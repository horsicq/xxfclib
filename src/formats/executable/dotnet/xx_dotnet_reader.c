/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/executable/dotnet/xx_dotnet_reader.h"
#include "../../xx_executable_inspect_internal.h"

#define DOTNET_READER_MAX_CODE (16U * 1024U * 1024U)
#define DOTNET_READER_MAX_SECTIONS 64U
#define DOTNET_READER_MAX_CLAUSES 4096U

static bool reader_begin(xx_dotnet_inspection *state)
{
    if (!state || !state->pe.pInput || !state->pe.pInput->device || !state->cli.bValid) return false;
    state->pe.pInput->read_work = 0;
    state->pe.pInput->failed = false;
    return state->pe.pInput->size >= 0 && !xx_pd_is_stopped(state->pe.pInput->pd);
}
static bool reader_range(int64_t limit, int64_t offset, uint64_t size)
{
    return limit >= 0 && offset >= 0 && offset <= limit && size <= (uint64_t)(limit - offset);
}
static bool reader_row(xx_dotnet_inspection *state, unsigned table, uint32_t rid, int64_t *offset, uint32_t *size)
{
    const xx_dotnet_inspect_cli *cli = &state->cli;
    uint64_t extent, delta;
    int64_t start;
    if (table >= 64 || (table > XX_DOTNET_MDT_GenericParamConstraint && (table < XX_DOTNET_MDT_Document || table > XX_DOTNET_MDT_CustomDebugInformation)) || !rid || rid > cli->pRows[table] || cli->pRows[table] > 0xffffffU || cli->pElementSize[table] <= 0 ||
        !reader_range(state->pe.pInput->size, cli->nTablesOffset, (uint64_t)cli->nTablesSize))
        return false;
    start = cli->pTableOffset[table];
    if (start < cli->nTablesOffset) return false;
    extent = (uint64_t)cli->pRows[table] * (uint32_t)cli->pElementSize[table];
    if (!reader_range(cli->nTablesSize, start - cli->nTablesOffset, extent) || !reader_range(state->pe.pInput->size, start, extent)) return false;
    delta = (uint64_t)(rid - 1) * (uint32_t)cli->pElementSize[table];
    *offset = start + (int64_t)delta;
    *size = (uint32_t)cli->pElementSize[table];
    return true;
}
bool xx_dotnet_inspect_table_row(xx_dotnet_inspection *state, unsigned table, uint32_t rid, int64_t *offset, uint32_t *size)
{
    if (offset) *offset = 0;
    if (size) *size = 0;
    return reader_begin(state) && offset && size && reader_row(state, table, rid, offset, size);
}

bool xx_dotnet_inspect_blob(xx_dotnet_inspection *state, uint32_t index, int64_t *offset, uint32_t *size)
{
    int64_t start, available;
    uint8_t prefix[4];
    unsigned width;
    uint32_t length;
    if (offset) *offset = 0;
    if (size) *size = 0;
    if (!reader_begin(state) || !offset || !size || state->cli.nBlobSize <= 0 ||
        !reader_range(state->pe.pInput->size, state->cli.nBlobOffset, (uint64_t)state->cli.nBlobSize) || index >= (uint64_t)state->cli.nBlobSize)
        return false;
    start = state->cli.nBlobOffset + index;
    available = state->cli.nBlobSize - index;
    if (!xx_exec_read(state->pe.pInput, start, prefix, 1)) return false;
    if (!index) {
        if (prefix[0]) return false;
        *offset = start;
        return true;
    }
    if (!(prefix[0] & 0x80)) {
        width = 1;
        length = prefix[0];
    } else if ((prefix[0] & 0xc0) == 0x80) {
        width = 2;
        if (available < 2 || !xx_exec_read(state->pe.pInput, start + 1, prefix + 1, 1)) return false;
        length = ((uint32_t)(prefix[0] & 0x3f) << 8) | prefix[1];
        if (length < 0x80) return false;
    } else if ((prefix[0] & 0xe0) == 0xc0) {
        width = 4;
        if (available < 4 || !xx_exec_read(state->pe.pInput, start + 1, prefix + 1, 3)) return false;
        length = ((uint32_t)(prefix[0] & 0x1f) << 24) | ((uint32_t)prefix[1] << 16) | ((uint32_t)prefix[2] << 8) | prefix[3];
        if (length < 0x4000) return false;
    } else return false;
    if ((uint64_t)width + length > (uint64_t)available || state->pe.pInput->failed) return false;
    *offset = start + width;
    *size = length;
    return true;
}

/* The header and all extra sections must stay in the same original raw
 * backing range. A truncated raw tail reduces availability, never exposes
 * a following section, overlay or virtual zero-filled memory. */
static bool reader_raw_range(xx_dotnet_inspection *state, uint32_t rva, int64_t *offset, uint64_t *available)
{
    const xx_pe_inspection *pe = &state->pe;
    int i;
    uint64_t raw, count;
    if (!rva || !pe->bValid || pe->nSectionCount < 0 || pe->nSectionCount > 4096 || (pe->nSectionCount && !pe->pSections)) return false;
    if (rva < pe->nSizeOfHeaders && rva < (uint64_t)pe->pInput->size) {
        *offset = rva;
        count = (uint64_t)pe->nSizeOfHeaders - rva;
        *available = count < (uint64_t)(pe->pInput->size - rva) ? count : (uint64_t)(pe->pInput->size - rva);
        return true;
    }
    for (i = 0; i < pe->nSectionCount; ++i) {
        const xx_pe_inspect_section *section = &pe->pSections[i];
        uint64_t delta;
        if (rva < section->nVirtualAddress) continue;
        delta = (uint64_t)rva - section->nVirtualAddress;
        if (delta >= section->nSizeOfRawData) continue;
        raw = (uint64_t)section->nPointerToRawData + delta;
        if (raw >= (uint64_t)pe->pInput->size) return false;
        count = section->nSizeOfRawData - delta;
        if (count > UINT64_C(0x100000000) - rva) count = UINT64_C(0x100000000) - rva;
        *offset = (int64_t)raw;
        *available = count < (uint64_t)pe->pInput->size - raw ? count : (uint64_t)pe->pInput->size - raw;
        return true;
    }
    return false;
}
static uint16_t reader_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t reader_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool reader_token(xx_dotnet_inspection *state, uint32_t token, bool local)
{
    unsigned table = token >> 24;
    uint32_t size;
    int64_t offset;
    if (local ? table != XX_DOTNET_MDT_StandAloneSig : (table != XX_DOTNET_MDT_TypeDef && table != XX_DOTNET_MDT_TypeRef && table != XX_DOTNET_MDT_TypeSpec))
        return false;
    return reader_row(state, table, token & 0xffffffU, &offset, &size);
}
bool xx_dotnet_inspect_method_body_ex(xx_dotnet_inspection *state, uint32_t rva,
    uint32_t options, xx_dotnet_method_body *body)
{
    xx_dotnet_method_body value;
    uint8_t header[12];
    int64_t start;
    uint64_t available, extent;
    unsigned sections = 0, clauses = 0;
    uint32_t header_size;
    bool more;
    if (body) xx_rt_memset(body, 0, sizeof(*body));
    if ((options & ~XX_DOTNET_METHOD_BODY_ALLOW_UNALIGNED_FAT) || !reader_begin(state) || !body ||
        !reader_raw_range(state, rva, &start, &available) || !xx_exec_read(state->pe.pInput, start, header, 1)) return false;
    xx_rt_memset(&value, 0, sizeof(value));
    value.header_offset = start;
    if ((header[0] & 3) == 2) {
        header_size = 1;
        value.flags = 2;
        value.max_stack = 8;
        value.code_size = header[0] >> 2;
        more = false;
    } else if ((header[0] & 3) == 3) {
        uint16_t flags;
        if (((rva & 3) && !(options & XX_DOTNET_METHOD_BODY_ALLOW_UNALIGNED_FAT)) ||
            available < 12 || !xx_exec_read(state->pe.pInput, start, header, 12)) return false;
        flags = reader_u16(header);
        if ((flags >> 12) != 3 || (flags & 0x0fe4)) return false;
        header_size = 12;
        value.flags = flags & 0xfff;
        value.max_stack = reader_u16(header + 2);
        value.code_size = reader_u32(header + 4);
        value.local_signature_token = reader_u32(header + 8);
        value.init_locals = (flags & 0x10) != 0;
        more = (flags & 8) != 0;
        if (value.local_signature_token && !reader_token(state, value.local_signature_token, true)) return false;
    } else return false;
    extent = (uint64_t)header_size + value.code_size;
    if (value.code_size > DOTNET_READER_MAX_CODE || extent > available) return false;
    value.code_offset = start + header_size;
    while (more) {
        uint8_t section_header[4];
        uint32_t section_size, clause_size, count, i;
        bool fat;
        /* CLR section alignment applies to the loaded address, including when
         * compatibility admits an unaligned historical fat header. */
        extent = ((extent + (rva & 3) + 3) & ~UINT64_C(3)) - (rva & 3);
        if (++sections > DOTNET_READER_MAX_SECTIONS || extent > available || available - extent < 4 ||
            !xx_exec_read(state->pe.pInput, start + (int64_t)extent, section_header, 4))
            return false;
        if ((section_header[0] & 0x3f) != 1) return false; /* Only EH tables are defined here. */
        fat = (section_header[0] & 0x40) != 0;
        more = (section_header[0] & 0x80) != 0;
        section_size = fat ? (uint32_t)section_header[1] | ((uint32_t)section_header[2] << 8) | ((uint32_t)section_header[3] << 16) : section_header[1];
        clause_size = fat ? 24 : 12;
        if ((!fat && (section_header[2] || section_header[3])) || section_size < 4 || section_size > available - extent || (section_size - 4) % clause_size) return false;
        count = (section_size - 4) / clause_size;
        if (count > DOTNET_READER_MAX_CLAUSES - clauses) return false;
        clauses += count;
        value.has_exception_handlers = true;
        for (i = 0; i < count; ++i) {
            uint8_t clause[24];
            uint32_t flags, try_offset, try_size, handler_offset, handler_size, extra;
            if (!xx_exec_read(state->pe.pInput, start + (int64_t)extent + 4 + (int64_t)i * clause_size, clause, clause_size)) return false;
            if (fat) {
                flags = reader_u32(clause);
                try_offset = reader_u32(clause + 4);
                try_size = reader_u32(clause + 8);
                handler_offset = reader_u32(clause + 12);
                handler_size = reader_u32(clause + 16);
                extra = reader_u32(clause + 20);
            } else {
                flags = reader_u16(clause);
                try_offset = reader_u16(clause + 2);
                try_size = clause[4];
                handler_offset = reader_u16(clause + 5);
                handler_size = clause[7];
                extra = reader_u32(clause + 8);
            }
            if ((flags != 0 && flags != 1 && flags != 2 && flags != 4) || try_offset > value.code_size || try_size > value.code_size - try_offset ||
                handler_offset > value.code_size || handler_size > value.code_size - handler_offset || (flags == 1 && extra >= value.code_size) ||
                (flags == 0 && !reader_token(state, extra, false)))
                return false;
        }
        extent += section_size;
    }
    if (extent > UINT32_MAX || state->pe.pInput->failed || xx_pd_is_stopped(state->pe.pInput->pd)) return false;
    value.total_size = (uint32_t)extent;
    *body = value;
    return true;
}

bool xx_dotnet_inspect_method_body(xx_dotnet_inspection *state, uint32_t rva, xx_dotnet_method_body *body)
{
    return xx_dotnet_inspect_method_body_ex(state, rva, 0, body);
}

#include "xx_dotnet_schema.inc"

typedef struct md_text { xx_dotnet_metadata_column *column; size_t used; } md_text;
static void md_add(md_text *out, const char *text)
{
    size_t length = xx_rt_strlen(text), available = sizeof(out->column->text) - 1 - out->used;
    if (length > available) {
        length = available; out->column->truncated = true;
        while (length && (((unsigned char)text[length] & 0xc0) == 0x80)) --length;
    }
    if (length) xx_rt_memcpy(out->column->text + out->used, text, length);
    out->used += length;
    out->column->text[out->used] = 0;
}
static void md_number(md_text *out, uint32_t value, bool hex)
{
    char text[32];
    xx_rt_snprintf(text, sizeof(text), hex ? "0x%08x" : "%u", value);
    md_add(out, text);
}
static void md_byte(md_text *out, uint8_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[3] = {digits[value >> 4], digits[value & 15], 0};
    md_add(out, text);
}
static void md_invalid(md_text *out, const char *reason)
{
    out->column->valid = false;
    md_add(out, "<invalid "); md_add(out, reason); md_add(out, ">");
}
static void md_escape(md_text *out, const uint8_t *bytes, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        uint8_t c = bytes[i];
        if (c >= 0x20 && c < 0x7f && c != '\\') {
            char text[2] = {(char)c, 0}; md_add(out, text);
        } else if (c >= 0xc2 && c <= 0xf4) {
            size_t n = c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4, j;
            bool valid = n <= count - i;
            for (j = 1; valid && j < n; ++j) valid = (bytes[i+j] & 0xc0) == 0x80;
            if (valid && n >= 3) valid = !(c == 0xe0 && bytes[i+1] < 0xa0) && !(c == 0xed && bytes[i+1] >= 0xa0) &&
                !(c == 0xf0 && bytes[i+1] < 0x90) && !(c == 0xf4 && bytes[i+1] >= 0x90);
            if (valid) {
                char text[5]; xx_rt_memcpy(text, bytes+i, n); text[n] = 0;
                /* Do not split a UTF-8 code point at the display limit. */
                if (n > sizeof(out->column->text) - 1 - out->used) out->column->truncated = true;
                else md_add(out, text);
                i += n - 1;
            } else { md_add(out, "\\x"); md_byte(out, c); }
        } else { md_add(out, "\\x"); md_byte(out, c); }
    }
}
const char *xx_dotnet_metadata_table_name(unsigned table)
{
    return table < 64 ? md_schemas[table].name : NULL;
}
static unsigned md_width(const xx_dotnet_inspect_cli *cli, const md_field *field)
{
    unsigned table, pointer, i;
    const md_coded_schema *code;
    if (field->width) return field->width;
    switch (field->kind) {
        case XX_DOTNET_METADATA_STRING: return (unsigned)cli->nStringIndexSize;
        case XX_DOTNET_METADATA_GUID: return (unsigned)cli->nGuidIndexSize;
        case XX_DOTNET_METADATA_BLOB: return (unsigned)cli->nBlobIndexSize;
        case XX_DOTNET_METADATA_TABLE: return (unsigned)cli->pIndexSize[field->arg];
        case XX_DOTNET_METADATA_LIST:
            table = field->arg & 255; pointer = field->arg >> 8;
            return (unsigned)(cli->pIndexSize[table] > cli->pIndexSize[pointer] ? cli->pIndexSize[table] : cli->pIndexSize[pointer]);
        case XX_DOTNET_METADATA_CODED:
            code = &md_codes[field->arg];
            for (i = 0; i < code->count; ++i)
                if (code->tables[i] < 64 && cli->pRows[code->tables[i]] >= (UINT32_C(1) << (16-code->bits))) return 4;
            return 2;
        default: return 0;
    }
}
static void md_string(xx_dotnet_inspection *state, uint32_t index, md_text *out)
{
    uint8_t bytes[260];
    int64_t offset;
    uint64_t remaining;
    uint32_t scanned = 0;
    out->column->is_null = index == 0;
    if (!index && !state->cli.nStringsSize) return;
    if (state->cli.nStringsSize <= 0 || index >= (uint64_t)state->cli.nStringsSize ||
        !reader_range(state->pe.pInput->size, state->cli.nStringsOffset, (uint64_t)state->cli.nStringsSize)) {
        md_invalid(out, "#Strings index"); return;
    }
    offset = state->cli.nStringsOffset + index;
    out->column->offset = offset;
    remaining = (uint64_t)state->cli.nStringsSize - index;
    while (remaining && scanned < 65536) {
        size_t n = remaining > 256 ? 256 : (size_t)remaining, used = 0;
        if (!xx_exec_read(state->pe.pInput, offset + scanned, bytes, n)) { md_invalid(out, "#Strings read"); return; }
        while (used < n && bytes[used]) ++used;
        if (!index && used) { md_invalid(out, "#Strings reserved entry"); return; }
        /* Keep multibyte UTF-8 together across chunks. */
        if (used == n && n == 256 && remaining > n) {
            size_t keep = 0;
            while (keep < 3 && keep < used && (bytes[used-keep-1] & 0xc0) == 0x80) ++keep;
            if (keep < used && bytes[used-keep-1] >= 0xc2) n -= keep+1;
        }
        md_escape(out, bytes, used < n ? used : n);
        if (used < n) { out->column->size = scanned + (uint32_t)used; return; }
        scanned += (uint32_t)n; remaining -= n;
    }
    out->column->size = scanned;
    if (remaining) { out->column->truncated = true; md_add(out, "..."); }
    else md_invalid(out, "unterminated #Strings value");
}
static void md_guid(xx_dotnet_inspection *state, uint32_t index, md_text *out)
{
    uint8_t bytes[16];
    char text[40];
    uint64_t delta;
    out->column->is_null = index == 0;
    if (!index) { md_add(out, "null"); return; }
    delta = (uint64_t)(index-1) * 16;
    if (!reader_range(state->pe.pInput->size, state->cli.nGuidOffset, (uint64_t)state->cli.nGuidSize) ||
        state->cli.nGuidSize < 0 || delta > (uint64_t)state->cli.nGuidSize || (uint64_t)state->cli.nGuidSize - delta < 16 ||
        !xx_exec_read(state->pe.pInput, state->cli.nGuidOffset+(int64_t)delta, bytes, 16)) { md_invalid(out, "#GUID index"); return; }
    out->column->offset = state->cli.nGuidOffset+(int64_t)delta; out->column->size = 16;
    xx_rt_snprintf(text,sizeof(text),"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", reader_u32(bytes), reader_u16(bytes+4), reader_u16(bytes+6),
        bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
    md_add(out,text);
}
static void md_reference(xx_dotnet_inspection *state, unsigned table, uint32_t rid, bool list, md_text *out)
{
    int64_t offset; uint32_t size;
    unsigned i, pos = 0;
    uint8_t bytes[64];
    const md_schema *schema;
    if (!xx_dotnet_metadata_table_name(table)) { md_invalid(out,"table tag"); return; }
    out->column->is_null = rid == 0;
    if (!rid) { md_add(out,"null"); return; }
    md_add(out, md_schemas[table].name); md_add(out,"["); md_number(out,rid,false); md_add(out,"]");
    if (list && rid == state->cli.pRows[table] + 1) { md_add(out," (end)"); return; }
    if (!reader_row(state,table,rid,&offset,&size)) { md_invalid(out,"row index"); return; }
    out->column->token = (table << 24) | rid;
    out->column->offset = offset; out->column->size = size;
    if (size > sizeof(bytes) || !xx_exec_read(state->pe.pInput,offset,bytes,size)) { md_invalid(out,"row read"); return; }
    schema = &md_schemas[table];
    for (i=0; i<schema->count; ++i) {
        const md_field *field = &schema->fields[i];
        unsigned width = md_width(&state->cli,field);
        if (!width || width > size-pos) { md_invalid(out,"row layout"); return; }
        if (field->kind == XX_DOTNET_METADATA_STRING &&
            (!xx_rt_strcmp(field->name,"Name") || !xx_rt_strcmp(field->name,"TypeName"))) {
            xx_dotnet_metadata_column name;
            md_text text;
            xx_rt_memset(&name,0,sizeof(name)); name.valid = true; text.column=&name; text.used=0;
            md_string(state,width==2?reader_u16(bytes+pos):reader_u32(bytes+pos),&text);
            if (name.text[0]) { md_add(out," "); md_add(out,name.text); }
            if (name.truncated) out->column->truncated=true;
            break;
        }
        pos += width;
    }
}
typedef struct md_signature {
    xx_dotnet_inspection *state;
    const uint8_t *bytes;
    size_t size,pos;
    unsigned nodes;
    md_text *out;
} md_signature;
static bool ms_compressed(md_signature *s,uint32_t *value,unsigned *bits,bool canonical)
{
    uint8_t first; unsigned n,i;
    if(s->pos>=s->size) return false;
    first=s->bytes[s->pos++];
    if(!(first&0x80)) { *value=first; if(bits)*bits=7; return true; }
    if((first&0xc0)==0x80) { n=1; *value=first&0x3f; if(bits)*bits=14; }
    else if((first&0xe0)==0xc0) { n=3; *value=first&0x1f; if(bits)*bits=29; }
    else return false;
    if(n>s->size-s->pos) return false;
    for(i=0;i<n;++i) *value=(*value<<8)|s->bytes[s->pos++];
    return !canonical || (n==1 ? *value>=0x80 : *value>=0x4000);
}
static bool ms_unsigned(md_signature *s,uint32_t *value,unsigned *bits)
{ return ms_compressed(s,value,bits,true); }
static bool ms_type(md_signature *,unsigned);
static bool ms_method(md_signature *s,unsigned depth)
{
    unsigned flags, convention, i; uint32_t count,generics=0;
    if(depth>32 || s->pos>=s->size) return false;
    flags=s->bytes[s->pos++]; convention=flags&15;
    if((flags&0x80) || (convention>5 && convention!=9 && convention!=11)) return false;
    if((flags&0x10) && !ms_unsigned(s,&generics,NULL)) return false;
    if(!ms_unsigned(s,&count,NULL) || count>1024 || generics>1024) return false;
    if(flags&0x20) md_add(s->out,"instance ");
    if(flags&0x40) md_add(s->out,"explicitthis ");
    if(convention==5) md_add(s->out,"vararg ");
    if(generics) {md_add(s->out,"generic<");md_number(s->out,generics,false);md_add(s->out,"> ");}
    if(!ms_type(s,depth+1)) return false;
    md_add(s->out," (");
    for(i=0;i<count;++i) {
        if(i) md_add(s->out,", ");
        if(s->pos<s->size && s->bytes[s->pos]==0x41) {++s->pos;md_add(s->out,"..., ");}
        if(!ms_type(s,depth+1)) return false;
    }
    md_add(s->out,")"); return true;
}
static bool ms_type(md_signature *s,unsigned depth)
{
    static const char *const primitive[] = {NULL,"void","bool","char","int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64","string"};
    unsigned type,i; uint32_t value,count,rank; unsigned bits;
    if(depth>32 || ++s->nodes>2048 || s->pos>=s->size) return false;
    type=s->bytes[s->pos++];
    if(type<sizeof(primitive)/sizeof(primitive[0]) && primitive[type]) {md_add(s->out,primitive[type]);return true;}
    switch(type) {
        case 0x0f: if(!ms_type(s,depth+1))return false;md_add(s->out,"*");return true;
        case 0x10: if(!ms_type(s,depth+1))return false;md_add(s->out,"&");return true;
        case 0x11: case 0x12:
            if(!ms_unsigned(s,&value,NULL) || (value&3)>2 || !(value>>2))return false;
            md_add(s->out,type==0x11?"valuetype ":"class ");
            md_reference(s->state,md_codes[MC_TYPE].tables[value&3],value>>2,false,s->out);
            return s->out->column->valid;
        case 0x13: case 0x1e:
            if(!ms_unsigned(s,&value,NULL))return false;
            md_add(s->out,type==0x13?"!":"!!");md_number(s->out,value,false);return true;
        case 0x14:
            if(!ms_type(s,depth+1)||!ms_unsigned(s,&rank,NULL)||rank>32||!rank||!ms_unsigned(s,&count,NULL)||count>rank)return false;
            md_add(s->out,"[rank=");md_number(s->out,rank,false);md_add(s->out,"; sizes=");
            for(i=0;i<count;++i){if(!ms_unsigned(s,&value,NULL))return false;if(i)md_add(s->out,",");md_number(s->out,value,false);}
            if(!ms_unsigned(s,&count,NULL)||count>rank)return false;
            md_add(s->out,"; lower=");
            for(i=0;i<count;++i){int32_t signed_value;char text[32];if(!ms_compressed(s,&value,&bits,false))return false;
                signed_value=(int32_t)(value>>1);if(value&1)signed_value|=(int32_t)(~UINT32_C(0)<<(bits-1));
                if((bits==14&&signed_value>=-64&&signed_value<=63)||(bits==29&&signed_value>=-8192&&signed_value<=8191))return false;
                xx_rt_snprintf(text,sizeof(text),"%d",signed_value);if(i)md_add(s->out,",");md_add(s->out,text);}
            md_add(s->out,"]");return true;
        case 0x15:
            if(s->pos>=s->size || (s->bytes[s->pos]!=0x11&&s->bytes[s->pos]!=0x12)||!ms_type(s,depth+1)||!ms_unsigned(s,&count,NULL)||count>1024)return false;
            md_add(s->out,"<");for(i=0;i<count;++i){if(i)md_add(s->out,", ");if(!ms_type(s,depth+1))return false;}md_add(s->out,">");return true;
        case 0x16:md_add(s->out,"typedref");return true;
        case 0x18:md_add(s->out,"native int");return true;
        case 0x19:md_add(s->out,"native uint");return true;
        case 0x1b:md_add(s->out,"methodptr ");return ms_method(s,depth+1);
        case 0x1c:md_add(s->out,"object");return true;
        case 0x1d:if(!ms_type(s,depth+1))return false;md_add(s->out,"[]");return true;
        case 0x1f:case 0x20:
            md_add(s->out,type==0x1f?"modreq(":"modopt(");
            if(!ms_unsigned(s,&value,NULL)||(value&3)>2||!(value>>2))return false;
            md_reference(s->state,md_codes[MC_TYPE].tables[value&3],value>>2,false,s->out);md_add(s->out,") ");
            return s->out->column->valid&&ms_type(s,depth+1);
        case 0x45:md_add(s->out,"pinned ");return ms_type(s,depth+1);
        default:return false;
    }
}
static bool md_signature_decode(xx_dotnet_inspection *state,unsigned kind,const uint8_t *bytes,size_t size,md_text *out)
{
    md_signature s;uint32_t count;unsigned i,flags;
    s.state=state;s.bytes=bytes;s.size=size;s.pos=0;s.nodes=0;s.out=out;
    if(!size)return false;
    if(kind==MB_TYPE) {if(!ms_type(&s,0))return false;}
    else if(kind==MB_FIELD || ((kind==MB_METHOD||kind==MB_LOCAL) && bytes[0]==6)) {
        if(s.bytes[s.pos++]!=6||!ms_type(&s,0))return false;
        if(kind==MB_LOCAL)md_invalid(out,"field signature in StandAloneSig; expected method/locals");
    } else if(kind==MB_LOCAL && bytes[0]==7) {
        ++s.pos;if(!ms_unsigned(&s,&count,NULL)||count>1024)return false;md_add(out,"locals (");
        for(i=0;i<count;++i){if(i)md_add(out,", ");if(!ms_type(&s,0))return false;}md_add(out,")");
    } else if(kind==MB_INSTANTIATION) {
        if(s.bytes[s.pos++]!=0x0a||!ms_unsigned(&s,&count,NULL)||count>1024)return false;md_add(out,"<");
        for(i=0;i<count;++i){if(i)md_add(out,", ");if(!ms_type(&s,0))return false;}md_add(out,">");
    } else if(kind==MB_PROPERTY) {
        flags=s.bytes[s.pos++];if((flags&~0x20U)!=8||!ms_unsigned(&s,&count,NULL)||count>1024)return false;
        if(flags&0x20)md_add(out,"instance ");if(!ms_type(&s,0))return false;md_add(out," property(");
        for(i=0;i<count;++i){if(i)md_add(out,", ");if(!ms_type(&s,0))return false;}md_add(out,")");
    } else if(!ms_method(&s,0))return false;
    return s.pos==s.size;
}
static void md_codepoint(md_text *out,uint32_t value)
{
    char text[12];
    if(value<0x20||(value>=0x7f&&value<=0x9f)||value==0x2028||value==0x2029) {
        xx_rt_snprintf(text,sizeof(text),"\\u%04x",value);md_add(out,text);
    } else if(value=='"'||value=='\\') {text[0]='\\';text[1]=(char)value;text[2]=0;md_add(out,text);}
    else {
        unsigned n=0;
        if(value<0x80)text[n++]=(char)value;
        else if(value<0x800){text[n++]=(char)(0xc0|(value>>6));text[n++]=(char)(0x80|(value&63));}
        else if(value<0x10000){text[n++]=(char)(0xe0|(value>>12));text[n++]=(char)(0x80|((value>>6)&63));text[n++]=(char)(0x80|(value&63));}
        else{text[n++]=(char)(0xf0|(value>>18));text[n++]=(char)(0x80|((value>>12)&63));text[n++]=(char)(0x80|((value>>6)&63));text[n++]=(char)(0x80|(value&63));}
        text[n]=0;md_add(out,text);
    }
}
static void md_constant(xx_dotnet_inspection *state,uint32_t type,int64_t offset,uint32_t size,md_text *out)
{
    static const char *const names[]={NULL,NULL,"bool","char","int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64","string"};
    static const unsigned widths[]={0,0,1,2,1,1,2,2,4,4,8,8,4,8};
    uint8_t bytes[256];uint64_t value=0;unsigned i;char text[96];
    if(type==0x12){
        if(size!=4||!xx_exec_read(state->pe.pInput,offset,bytes,4)||reader_u32(bytes))md_invalid(out,"null class constant");
        else md_add(out,"nullref");return;
    }
    if(type>=sizeof(names)/sizeof(names[0])||!names[type]){md_invalid(out,"constant type");return;}
    md_add(out,names[type]);md_add(out," ");
    if(type==0x0e){
        uint32_t position=0,high=0;
        if(size&1){md_invalid(out,"UTF-16 constant length");return;}
        md_add(out,"\"");
        while(position<size&&position<65536){
            unsigned n=size-position>sizeof(bytes)?sizeof(bytes):size-position;
            if(!xx_exec_read(state->pe.pInput,offset+position,bytes,n)){md_invalid(out,"constant read");return;}
            for(i=0;i<n;i+=2){uint32_t unit=reader_u16(bytes+i);
                if(high){if(unit<0xdc00||unit>0xdfff){md_invalid(out,"UTF-16 surrogate");return;}md_codepoint(out,0x10000+((high-0xd800)<<10)+(unit-0xdc00));high=0;}
                else if(unit>=0xd800&&unit<=0xdbff)high=unit;
                else if(unit>=0xdc00&&unit<=0xdfff){md_invalid(out,"UTF-16 surrogate");return;}
                else md_codepoint(out,unit);
            }
            position+=n;
        }
        if(position<size)out->column->truncated=true;
        else if(high)md_invalid(out,"UTF-16 surrogate");
        md_add(out,"\"");return;
    }
    if(size!=widths[type]||!xx_exec_read(state->pe.pInput,offset,bytes,size)){md_invalid(out,"constant size/read");return;}
    for(i=0;i<size;++i)value|=(uint64_t)bytes[i]<<(i*8);
    switch(type){
        case 2:if(value>1)md_invalid(out,"boolean constant");else md_add(out,value?"true":"false");return;
        case 3:md_add(out,"'");md_codepoint(out,(uint32_t)value);md_add(out,"'");return;
        case 4:xx_rt_snprintf(text,sizeof(text),"%d",(int)(int8_t)value);break;
        case 6:xx_rt_snprintf(text,sizeof(text),"%d",(int)(int16_t)value);break;
        case 8:xx_rt_snprintf(text,sizeof(text),"%d",(int32_t)value);break;
        case 10:xx_rt_snprintf(text,sizeof(text),"%lld",(long long)(int64_t)value);break;
        case 12:{uint32_t bits=(uint32_t)value;float f;xx_rt_memcpy(&f,&bits,4);xx_rt_snprintf(text,sizeof(text),"%.9g",(double)f);break;}
        case 13:{double f;xx_rt_memcpy(&f,&value,8);xx_rt_snprintf(text,sizeof(text),"%.17g",f);break;}
        default:xx_rt_snprintf(text,sizeof(text),"%llu",(unsigned long long)value);break;
    }
    md_add(out,text);
}
static void md_blob_value(xx_dotnet_inspection *state,uint32_t index,unsigned kind,uint32_t context,md_text *out)
{
    int64_t offset;uint32_t size,i,n;uint8_t bytes[4096];
    out->column->is_null=index==0;
    if(!index && !state->cli.nBlobSize){md_add(out,"empty");return;}
    /* This query resets only read accounting; the view and both cursors stay
     * unchanged. A heap error remains visible in the decoded column. */
    if(!xx_dotnet_inspect_blob(state,index,&offset,&size)){md_invalid(out,"#Blob index/length");return;}
    out->column->offset=offset;out->column->size=size;
    if(kind==MB_CONSTANT){md_constant(state,context,offset,size,out);return;}
    if(kind>=MB_METHOD&&kind<=MB_INSTANTIATION) {
        if(size>sizeof(bytes)){out->column->truncated=true;md_add(out,"signature exceeds 4096-byte decoding limit; ");}
        else if(size && xx_exec_read(state->pe.pInput,offset,bytes,size)&&md_signature_decode(state,kind,bytes,size,out))return;
        else {md_invalid(out,"signature");md_add(out,"; ");}
    }
    if(kind==MB_ATTRIBUTE && size>=2) {
        if(!xx_exec_read(state->pe.pInput,offset,bytes,2)||reader_u16(bytes)!=1)md_invalid(out,"custom attribute prolog");
        else md_add(out,"prolog=0x0001; ");
    }
    md_number(out,size,false);md_add(out," bytes: ");n=size>32?32:size;
    if(n&&!xx_exec_read(state->pe.pInput,offset,bytes,n)){md_invalid(out,"blob read");return;}
    for(i=0;i<n;++i){if(i)md_add(out," ");md_byte(out,bytes[i]);}
    if(n<size){out->column->truncated=true;md_add(out," ...");}
}
bool xx_dotnet_inspect_decode_row(xx_dotnet_inspection *state,unsigned table,uint32_t rid,xx_dotnet_metadata_row *row)
{
    uint8_t bytes[64];unsigned i,pos=0;int64_t offset;uint32_t size;
    const md_schema *schema;
    if(row)xx_rt_memset(row,0,sizeof(*row));
    if(!row||!reader_begin(state)||!xx_dotnet_metadata_table_name(table)||!reader_row(state,table,rid,&offset,&size)||size>sizeof(bytes)||
        !xx_exec_read(state->pe.pInput,offset,bytes,size))return false;
    schema=&md_schemas[table];row->name=schema->name;row->table=table;row->token=(table<<24)|rid;row->offset=offset;row->size=size;row->count=schema->count;row->valid=true;
    for(i=0;i<schema->count;++i) {
        const md_field *field=&schema->fields[i];xx_dotnet_metadata_column *column=&row->columns[i];md_text out;
        unsigned width=md_width(&state->cli,field),target;uint32_t raw,number;const md_coded_schema *code;
        if((width!=2&&width!=4)||pos>size||width>size-pos){xx_rt_memset(row,0,sizeof(*row));return false;}
        raw=width==2?reader_u16(bytes+pos):reader_u32(bytes+pos);pos+=width;
        column->name=field->name;column->kind=field->kind;column->raw=raw;column->valid=true;out.column=column;out.used=0;
        switch(field->kind) {
            case XX_DOTNET_METADATA_STRING:md_string(state,raw,&out);break;
            case XX_DOTNET_METADATA_GUID:md_guid(state,raw,&out);break;
            case XX_DOTNET_METADATA_BLOB:md_blob_value(state,raw,field->arg,table==XX_DOTNET_MDT_Constant?row->columns[0].raw:0,&out);break;
            case XX_DOTNET_METADATA_TABLE:md_reference(state,field->arg,raw,false,&out);break;
            case XX_DOTNET_METADATA_LIST:
                target=field->arg&255;if(state->cli.pRows[field->arg>>8])target=field->arg>>8;
                md_reference(state,target,raw,true,&out);break;
            case XX_DOTNET_METADATA_CODED:
                code=&md_codes[field->arg];number=raw>>code->bits;target=raw&((1U<<code->bits)-1);
                if(!raw){column->is_null=true;md_add(&out,"null");}
                else if(target>=code->count||code->tables[target]>=64||!number)md_invalid(&out,"coded index tag/RID");
                else md_reference(state,code->tables[target],number,false,&out);
                break;
            case XX_DOTNET_METADATA_TOKEN:
                if(!raw){column->is_null=true;md_add(&out,"null");}
                else md_reference(state,raw>>24,raw&0xffffff,false,&out);
                break;
            default:md_number(&out,raw,field->kind==XX_DOTNET_METADATA_FLAGS);break;
        }
        if(!column->valid)row->valid=false;
        if(xx_pd_is_stopped(state->pe.pInput->pd)){xx_rt_memset(row,0,sizeof(*row));return false;}
    }
    if(pos!=size){xx_rt_memset(row,0,sizeof(*row));return false;}
    return true;
}
