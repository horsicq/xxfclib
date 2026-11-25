/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft Portable PDB generic dump and embedded sources reader:
 * ECMA-335 ("BSJB") metadata root, stream headers, #Pdb stream,
 * #~ / #- metadata tables, heaps, sequence points, local scopes,
 * variables, constants, import scopes, state machine methods,
 * custom debug information, and embedded source inflation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_debug.h"
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_portable.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/io/xx_io.h"

#define PDUMP_MAX_STREAM_SIZE (256u * 1024u * 1024u)
#define PDUMP_BLOB_CAP 4096u

/* ---- Little-Endian Helpers ---------------------------------------------- */

static uint8_t pdump_u8(const uint8_t *data, size_t size, size_t offset)
{
    if (offset < size) {
        return data[offset];
    }
    return 0;
}

static uint16_t pdump_u16(const uint8_t *data, size_t size, size_t offset)
{
    if (offset + 1 < size) {
        return (uint16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8));
    }
    return 0;
}

static uint32_t pdump_u32(const uint8_t *data, size_t size, size_t offset)
{
    if (offset + 3 < size) {
        return (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
    }
    return 0;
}

static uint64_t pdump_u64(const uint8_t *data, size_t size, size_t offset)
{
    if (offset + 7 < size) {
        uint64_t lo = pdump_u32(data, size, offset);
        uint64_t hi = pdump_u32(data, size, offset + 4);
        return lo | (hi << 32);
    }
    return 0;
}

static int32_t pdump_i32(const uint8_t *data, size_t size, size_t offset)
{
    return (int32_t)pdump_u32(data, size, offset);
}

static void pdump_format_guid(char *out, size_t out_sz, const uint8_t *g)
{
    uint32_t d1 = (uint32_t)g[0] | ((uint32_t)g[1] << 8) | ((uint32_t)g[2] << 16) | ((uint32_t)g[3] << 24);
    uint16_t d2 = (uint16_t)((uint16_t)g[4] | ((uint16_t)g[5] << 8));
    uint16_t d3 = (uint16_t)((uint16_t)g[6] | ((uint16_t)g[7] << 8));
    snprintf(out, out_sz, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", d1, d2, d3, g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

static int32_t pdump_read_compressed_uint(const uint8_t *data, size_t size, size_t offset, uint32_t *out_val)
{
    if (out_val) *out_val = 0;
    if (!data || offset >= size || size > 0x7FFFFFFF) return 0;
    return xx_ms_pdb_portable_read_compressed_uint(data, (int32_t)size, (int32_t)offset, out_val);
}

static int32_t pdump_read_compressed_int(const uint8_t *data, size_t size, size_t offset, int32_t *out_val)
{
    uint32_t u = 0;
    int32_t len = pdump_read_compressed_uint(data, size, offset, &u);
    if (len <= 0) {
        if (out_val) *out_val = 0;
        return 0;
    }
    bool is_neg = (u & 1) != 0;
    int32_t val = (int32_t)(u >> 1);
    if (is_neg) {
        if (len == 1) {
            val |= -64;
        } else if (len == 2) {
            val |= -8192;
        } else {
            val |= -268435456;
        }
    }
    if (out_val) *out_val = val;
    return len;
}

/* ---- Context for Event Emission ----------------------------------------- */

typedef struct pdump_ctx {
    xx_ms_pdb_dump_fn fn;
    void *user;
    int32_t depth;
    bool stopped;
} pdump_ctx;

static void pdump_begin(pdump_ctx *ctx, const char *name, int32_t stream, int64_t offset)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_BEGIN;
    ev.value_kind = XX_MS_PDB_FV_NONE;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.s = "";
    ev.stream = stream;
    ev.offset = offset;
    if (!ctx->fn(ctx->user, &ev)) {
        ctx->stopped = true;
    }
    ctx->depth++;
}

static void pdump_end(pdump_ctx *ctx, const char *name, int32_t stream, int64_t offset)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    if (ctx->depth > 0) ctx->depth--;
    ev.event = XX_MS_PDB_EV_END;
    ev.value_kind = XX_MS_PDB_FV_NONE;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.s = "";
    ev.stream = stream;
    ev.offset = offset;
    if (!ctx->fn(ctx->user, &ev)) {
        ctx->stopped = true;
    }
}

static void pdump_field_uint(pdump_ctx *ctx, const char *name, uint64_t val)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_UINT;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.u = val;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void pdump_field_int(pdump_ctx *ctx, const char *name, int64_t val)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_INT;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.i = val;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void pdump_field_hex(pdump_ctx *ctx, const char *name, uint64_t val)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_HEX;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.u = val;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void pdump_field_string(pdump_ctx *ctx, const char *name, const char *str)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_STRING;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.s = str ? str : "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void pdump_field_bool(pdump_ctx *ctx, const char *name, bool val)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_BOOL;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.u = val ? 1 : 0;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void pdump_field_enum(pdump_ctx *ctx, const char *name, uint64_t val, const char *enum_str)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_ENUM;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.u = val;
    ev.s = enum_str ? enum_str : "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void pdump_field_guid(pdump_ctx *ctx, const char *name, const char *guid_str)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_GUID;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.s = guid_str ? guid_str : "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

static void pdump_field_bytes(pdump_ctx *ctx, const char *name, const uint8_t *bytes, size_t size)
{
    xx_ms_pdb_dump_event ev = {0};
    if (ctx->stopped) return;
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_BYTES;
    ev.depth = ctx->depth;
    ev.name = name;
    ev.bytes = bytes;
    ev.size = size;
    ev.s = "";
    ev.stream = -1;
    ev.offset = -1;
    if (!ctx->fn(ctx->user, &ev)) ctx->stopped = true;
}

/* ---- Well-known GUIDs and Names ----------------------------------------- */

static const struct {
    uint8_t guid[16];
    const char *name;
} pdump_known_guids[] = {
    /* Hash algorithms */
    {{0xEC, 0x16, 0x18, 0xFF, 0x5E, 0xAA, 0x10, 0x4D, 0x87, 0xF7, 0x6F, 0x49, 0x63, 0x83, 0x34, 0x60}, "SHA1"},
    {{0x0F, 0xD0, 0x29, 0x88, 0xB8, 0x11, 0x13, 0x42, 0x87, 0x8B, 0x77, 0x0E, 0x85, 0x97, 0xAC, 0x16}, "SHA256"},

    /* Languages */
    {{0xF8, 0x62, 0x51, 0x3F, 0xC6, 0x07, 0xD3, 0x11, 0x90, 0x53, 0x00, 0xC0, 0x4F, 0xA3, 0x02, 0xA1}, "C#"},
    {{0xB8, 0xD0, 0x12, 0x3A, 0x6C, 0xC2, 0xD0, 0x11, 0xB4, 0x42, 0x00, 0xA0, 0x24, 0x4A, 0x1D, 0xD2}, "VB"},
    {{0xC9, 0x38, 0x4F, 0xAB, 0xE6, 0xB6, 0xBA, 0x43, 0xBE, 0x3B, 0x58, 0x08, 0x0B, 0x2C, 0xCC, 0xE3}, "F#"},

    /* Custom Debug Info kinds */
    {{0x56, 0x05, 0x11, 0xCC, 0x91, 0xA0, 0x38, 0x4D, 0x9F, 0xEC, 0x25, 0xAB, 0x9A, 0x35, 0x1A, 0x6A}, "SourceLink"},
    {{0x1B, 0x57, 0x8A, 0x0E, 0x26, 0x69, 0x6E, 0x46, 0xB4, 0xAD, 0x8A, 0xB0, 0x46, 0x11, 0xF5, 0xFE}, "EmbeddedSource"},
    {{0x08, 0x47, 0x4D, 0x7E, 0x6E, 0x09, 0x5C, 0x4C, 0xAE, 0xDA, 0xCB, 0x10, 0xBA, 0x6A, 0x74, 0x0D}, "CompilationMetadataReferences"},
    {{0x05, 0xEC, 0xFE, 0xB5, 0xD0, 0x8C, 0x83, 0x4A, 0x96, 0xDA, 0x46, 0x62, 0x84, 0xBB, 0x4B, 0xD8}, "CompilationOptions"},
    {{0xB6, 0xEA, 0xB2, 0x58, 0x9F, 0x20, 0x4E, 0x4E, 0xA2, 0x2C, 0xB2, 0xD0, 0xF9, 0x10, 0xC7, 0x82}, "DefaultNamespace"},
    {{0xC4, 0x63, 0xC5, 0x83, 0xF3, 0xB4, 0xD5, 0x47, 0xB8, 0x24, 0xBA, 0x54, 0x41, 0x47, 0x7E, 0xA8}, "DynamicLocalVariables"},
    {{0x71, 0xDF, 0x9F, 0xED, 0x79, 0x88, 0x47, 0x47, 0x8E, 0xD3, 0xFE, 0x5E, 0xDE, 0x3C, 0xE7, 0x10}, "TupleElementNames"},
    {{0x1E, 0xA6, 0xA9, 0x6D, 0xC7, 0xF8, 0x74, 0x48, 0xBE, 0x62, 0x68, 0xBC, 0x56, 0x30, 0xDF, 0x71}, "StateMachineHoistedLocalScopes"},
    {{0xA8, 0x52, 0x5F, 0x75, 0xC5, 0x91, 0xBE, 0x45, 0xB4, 0xB8, 0x20, 0x95, 0x71, 0xE5, 0x52, 0xBD}, "EncLocalSlotMap"},
    {{0x4C, 0x00, 0x43, 0xA6, 0x40, 0x02, 0x6F, 0x49, 0xA7, 0x83, 0x30, 0xD6, 0x4F, 0x49, 0x79, 0xDE}, "EncLambdaAndClosureMap"},
    {{0xBC, 0x74, 0x2E, 0x93, 0xA9, 0xDB, 0x78, 0x44, 0x8D, 0x46, 0x0F, 0x32, 0xA7, 0xBA, 0xB3, 0xD3}, "TypeDefinitionDocuments"},
    {{0xC5, 0x2A, 0xFD, 0x54, 0x25, 0xE9, 0x1A, 0x40, 0x9C, 0x2A, 0xF9, 0x4F, 0x17, 0x10, 0x72, 0xF8}, "PrimaryConstructorInformationBlob"}};

static const char *pdump_guid_name(const uint8_t *guid)
{
    if (!guid) return "";
    for (size_t i = 0; i < sizeof(pdump_known_guids) / sizeof(pdump_known_guids[0]); i++) {
        if (memcmp(pdump_known_guids[i].guid, guid, 16) == 0) {
            return pdump_known_guids[i].name;
        }
    }
    return "";
}

/* HasCustomDebugInformation tag mapping (5 bits) to ECMA-335 table IDs */
static const uint32_t pdump_has_cdi_tables[32] = {
    0x06,                        /* 0: MethodDef */
    0x04,                        /* 1: Field */
    0x01,                        /* 2: TypeRef */
    0x02,                        /* 3: TypeDef */
    0x08,                        /* 4: Param */
    0x09,                        /* 5: InterfaceImpl */
    0x0A,                        /* 6: MemberRef */
    0x00,                        /* 7: Module */
    0x0E,                        /* 8: DeclSecurity */
    0x17,                        /* 9: Property */
    0x14,                        /* 10: Event */
    0x11,                        /* 11: StandAloneSig */
    0x1A,                        /* 12: ModuleRef */
    0x1B,                        /* 13: TypeSpec */
    0x20,                        /* 14: Assembly */
    0x23,                        /* 15: AssemblyRef */
    0x26,                        /* 16: File */
    0x27,                        /* 17: ExportedType */
    0x28,                        /* 18: ManifestResource */
    0x2A,                        /* 19: GenericParam */
    0x2C,                        /* 20: GenericParamConstraint */
    0x2B,                        /* 21: MethodSpec */
    0x30,                        /* 22: Document */
    0x32,                        /* 23: LocalScope */
    0x33,                        /* 24: LocalVariable */
    0x34,                        /* 25: LocalConstant */
    0x35,                        /* 26: ImportScope */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF /* 27..31: unused */
};

static uint32_t pdump_decode_has_cdi(uint32_t coded_index)
{
    uint32_t tag = coded_index & 0x1F;
    uint32_t row = coded_index >> 5;
    uint32_t tbl = pdump_has_cdi_tables[tag];
    if (tbl == 0xFF) return 0;
    return (tbl << 24) | row;
}

/* ImportDefinitionKind names */
static const char *pdump_import_kind_name(uint32_t kind)
{
    switch (kind) {
        case 1: return "ImportNamespace";
        case 2: return "ImportAssemblyNamespace";
        case 3: return "ImportType";
        case 4: return "ImportXmlNamespace";
        case 5: return "ImportAssemblyReferenceAlias";
        case 6: return "AliasAssemblyReference";
        case 7: return "AliasNamespace";
        case 8: return "AliasAssemblyNamespace";
        case 9: return "AliasType";
        default: return "Unknown";
    }
}

/* LocalConstant signature ElementType names */
static const char *pdump_element_type_name(uint8_t et)
{
    switch (et) {
        case 0x01: return "Boolean";
        case 0x02: return "Char";
        case 0x03: return "I1";
        case 0x04: return "U1";
        case 0x05: return "I2";
        case 0x06: return "U2";
        case 0x07: return "I4";
        case 0x08: return "U4";
        case 0x09: return "I8";
        case 0x0A: return "U8";
        case 0x0B: return "R4";
        case 0x0C: return "R8";
        case 0x0E: return "String";
        case 0x11: return "ValueType";
        case 0x12: return "Class";
        case 0x18: return "I";
        case 0x19: return "U";
        case 0x1C: return "Object";
        default: return "Unknown";
    }
}

/* ---- Document Name Helper ----------------------------------------------- */

static bool pdump_get_blob(const uint8_t *blob_data, size_t blob_size, uint32_t index, const uint8_t **out_payload, uint32_t *out_len)
{
    if (out_payload) *out_payload = NULL;
    if (out_len) *out_len = 0;
    if (!blob_data || (size_t)index >= blob_size) return false;

    uint32_t len = 0;
    int32_t len_bytes = pdump_read_compressed_uint(blob_data, blob_size, index, &len);
    if (len_bytes <= 0) return false;
    if ((size_t)index + (size_t)len_bytes + (size_t)len > blob_size) return false;

    if (out_payload) *out_payload = blob_data + index + len_bytes;
    if (out_len) *out_len = len;
    return true;
}

static char *pdump_decode_doc_name(const uint8_t *blob_data, size_t blob_size, uint32_t name_blob_idx)
{
    const uint8_t *raw_name = NULL;
    uint32_t raw_len = 0;
    if (!pdump_get_blob(blob_data, blob_size, name_blob_idx, &raw_name, &raw_len)) {
        return NULL;
    }
    if (raw_len == 0 || !raw_name) {
        char *empty = (char *)xx_mem_alloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    uint8_t sep = raw_name[0];
    size_t off = 1;

    size_t cap = 256;
    size_t len = 0;
    char *res = (char *)xx_mem_alloc(cap);
    if (!res) return NULL;
    res[0] = '\0';

    bool first = true;
    while (off < raw_len) {
        uint32_t part_idx = 0;
        int32_t pb = pdump_read_compressed_uint(raw_name, raw_len, off, &part_idx);
        if (pb <= 0) break;
        off += (size_t)pb;

        if (!first && sep != 0) {
            if (len + 2 > cap) {
                cap *= 2;
                char *new_res = (char *)xx_mem_realloc(res, cap);
                if (!new_res) {
                    xx_mem_free(res);
                    return NULL;
                }
                res = new_res;
            }
            res[len++] = (char)sep;
            res[len] = '\0';
        }
        first = false;

        if (part_idx != 0) {
            const uint8_t *part_data = NULL;
            uint32_t part_len = 0;
            if (pdump_get_blob(blob_data, blob_size, part_idx, &part_data, &part_len) && part_len > 0) {
                while (len + part_len + 1 > cap) {
                    cap *= 2;
                }
                char *new_res = (char *)xx_mem_realloc(res, cap);
                if (!new_res) {
                    xx_mem_free(res);
                    return NULL;
                }
                res = new_res;
                xx_mem_copy(res + len, part_data, part_len);
                len += part_len;
                res[len] = '\0';
            }
        }
    }

    return res;
}

/* ---- Public API: xx_ms_pdb_portable_dump -------------------------------- */

XXFC_API bool xx_ms_pdb_portable_dump(xx_io_device *device, xx_ms_pdb_dump_fn fn, void *user)
{
    if (!device || !fn) return false;

    pdump_ctx ctx = {0};
    ctx.fn = fn;
    ctx.user = user;
    ctx.depth = 0;
    ctx.stopped = false;

    xx_ms_pdb_portable_details info = {0};
    if (!xx_ms_pdb_portable_read_info(device, &info)) {
        return false;
    }

    /* 1. metadata_root */
    pdump_begin(&ctx, "metadata_root", -1, 0);
    pdump_field_hex(&ctx, "signature", XX_MS_PDB_PORTABLE_SIGNATURE);
    pdump_field_uint(&ctx, "major_version", info.major_version);
    pdump_field_uint(&ctx, "minor_version", info.minor_version);
    pdump_field_uint(&ctx, "reserved", 0);
    pdump_field_string(&ctx, "version", info.version ? info.version : "");
    pdump_field_hex(&ctx, "flags", info.flags);
    pdump_field_uint(&ctx, "number_of_streams", info.number_of_streams);

    for (size_t i = 0; i < info.stream_count; i++) {
        pdump_begin(&ctx, "stream", (int32_t)i, info.streams[i].header_offset);
        pdump_field_hex(&ctx, "offset", info.streams[i].offset);
        pdump_field_uint(&ctx, "size", info.streams[i].size);
        pdump_field_string(&ctx, "name", info.streams[i].name ? info.streams[i].name : "");
        pdump_end(&ctx, "stream", (int32_t)i, info.streams[i].header_offset);
    }
    pdump_end(&ctx, "metadata_root", -1, 0);

    /* 2. #Pdb stream */
    const xx_ms_pdb_portable_stream *pdb_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_PDB);
    if (pdb_stream && info.pdb_stream_valid) {
        pdump_begin(&ctx, "pdb_stream", -1, pdb_stream->offset);
        pdump_field_guid(&ctx, "id", info.pdb_id ? info.pdb_id : "");
        pdump_field_hex(&ctx, "stamp", info.pdb_stamp);
        pdump_field_hex(&ctx, "entry_point", info.entry_point);
        pdump_field_hex(&ctx, "referenced_type_system_tables", info.referenced_type_system_tables);

        for (uint32_t t = 0; t < XX_MS_PDB_TABLE_MAX; t++) {
            if (info.referenced_type_system_tables & ((uint64_t)1 << t)) {
                pdump_begin(&ctx, "type_system_table", -1, -1);
                pdump_field_uint(&ctx, "table_id", t);
                pdump_field_string(&ctx, "table_name", xx_ms_pdb_portable_table_name(t));
                pdump_field_uint(&ctx, "rows", info.type_system_table_rows[t]);
                pdump_end(&ctx, "type_system_table", -1, -1);
            }
        }
        pdump_end(&ctx, "pdb_stream", -1, pdb_stream->offset);
    }

    /* 3. Read metadata heaps and tables into memory */
    const xx_ms_pdb_portable_stream *tbl_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_TABLES);
    if (!tbl_stream) {
        tbl_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_TABLES_UNCOMPRESSED);
    }
    const xx_ms_pdb_portable_stream *str_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_STRINGS);
    const xx_ms_pdb_portable_stream *blob_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_BLOB);
    const xx_ms_pdb_portable_stream *guid_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_GUID);
    const xx_ms_pdb_portable_stream *us_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_USERSTRINGS);

    uint8_t *tbl_data = NULL;
    size_t tbl_size = 0;
    uint8_t *str_data = NULL;
    size_t str_size = 0;
    uint8_t *blob_data = NULL;
    size_t blob_size = 0;
    uint8_t *guid_data = NULL;
    size_t guid_size = 0;

    int64_t dev_total = xx_io_total_size(device);

    if (tbl_stream && tbl_stream->size > 0 && tbl_stream->size <= PDUMP_MAX_STREAM_SIZE) {
        tbl_data = (uint8_t *)xx_mem_alloc(tbl_stream->size);
        if (tbl_data && xx_io_seek64(device, tbl_stream->offset, SEEK_SET) == 0) {
            ssize_t n = xx_io_read(device, tbl_data, tbl_stream->size);
            if (n > 0) tbl_size = (size_t)n;
        }
    }
    if (str_stream && str_stream->size > 0 && str_stream->size <= PDUMP_MAX_STREAM_SIZE) {
        str_data = (uint8_t *)xx_mem_alloc(str_stream->size + 1);
        if (str_data && xx_io_seek64(device, str_stream->offset, SEEK_SET) == 0) {
            ssize_t n = xx_io_read(device, str_data, str_stream->size);
            if (n > 0) {
                str_size = (size_t)n;
                str_data[str_size] = '\0';
            }
        }
    }
    if (blob_stream && blob_stream->size > 0 && blob_stream->size <= PDUMP_MAX_STREAM_SIZE) {
        blob_data = (uint8_t *)xx_mem_alloc(blob_stream->size);
        if (blob_data && xx_io_seek64(device, blob_stream->offset, SEEK_SET) == 0) {
            ssize_t n = xx_io_read(device, blob_data, blob_stream->size);
            if (n > 0) blob_size = (size_t)n;
        }
    }
    if (guid_stream && guid_stream->size > 0 && guid_stream->size <= PDUMP_MAX_STREAM_SIZE) {
        guid_data = (uint8_t *)xx_mem_alloc(guid_stream->size);
        if (guid_data && xx_io_seek64(device, guid_stream->offset, SEEK_SET) == 0) {
            ssize_t n = xx_io_read(device, guid_data, guid_stream->size);
            if (n > 0) guid_size = (size_t)n;
        }
    }

    (void)dev_total;

    /* 4. Heaps */
    pdump_begin(&ctx, "strings_heap", -1, str_stream ? str_stream->offset : -1);
    pdump_field_uint(&ctx, "size", str_stream ? str_stream->size : 0);
    pdump_end(&ctx, "strings_heap", -1, str_stream ? str_stream->offset : -1);

    pdump_begin(&ctx, "blob_heap", -1, blob_stream ? blob_stream->offset : -1);
    pdump_field_uint(&ctx, "size", blob_stream ? blob_stream->size : 0);
    pdump_end(&ctx, "blob_heap", -1, blob_stream ? blob_stream->offset : -1);

    uint32_t guid_count = guid_stream ? (guid_stream->size / 16) : 0;
    pdump_begin(&ctx, "guid_heap", -1, guid_stream ? guid_stream->offset : -1);
    pdump_field_uint(&ctx, "size", guid_stream ? guid_stream->size : 0);
    pdump_field_uint(&ctx, "count", guid_count);
    for (uint32_t g = 1; g <= guid_count; g++) {
        pdump_begin(&ctx, "guid_entry", -1, -1);
        pdump_field_uint(&ctx, "index", g);
        if (guid_data && (size_t)g * 16 <= guid_size) {
            char gstr[40];
            pdump_format_guid(gstr, sizeof(gstr), guid_data + (g - 1) * 16);
            pdump_field_guid(&ctx, "guid", gstr);
        } else {
            pdump_field_guid(&ctx, "guid", "{00000000-0000-0000-0000-000000000000}");
        }
        pdump_end(&ctx, "guid_entry", -1, -1);
    }
    pdump_end(&ctx, "guid_heap", -1, guid_stream ? guid_stream->offset : -1);

    pdump_begin(&ctx, "us_heap", -1, us_stream ? us_stream->offset : -1);
    pdump_field_uint(&ctx, "size", us_stream ? us_stream->size : 0);
    pdump_end(&ctx, "us_heap", -1, us_stream ? us_stream->offset : -1);

    /* 5. Tables header */
    if (tbl_stream && info.tables_valid) {
        pdump_begin(&ctx, "tables_header", -1, tbl_stream->offset);
        pdump_field_uint(&ctx, "major_version", info.tables_major_version);
        pdump_field_uint(&ctx, "minor_version", info.tables_minor_version);
        pdump_field_hex(&ctx, "heap_sizes", info.heap_sizes);
        pdump_field_hex(&ctx, "valid_tables", info.valid_tables);
        pdump_field_hex(&ctx, "sorted_tables", info.sorted_tables);

        for (uint32_t t = 0; t < XX_MS_PDB_TABLE_MAX; t++) {
            if (info.valid_tables & ((uint64_t)1 << t)) {
                pdump_begin(&ctx, "table_info", -1, -1);
                pdump_field_uint(&ctx, "table_id", t);
                pdump_field_string(&ctx, "table_name", xx_ms_pdb_portable_table_name(t));
                pdump_field_uint(&ctx, "rows", info.table_rows[t]);
                pdump_end(&ctx, "table_info", -1, -1);
            }
        }
        pdump_end(&ctx, "tables_header", -1, tbl_stream->offset);
    }

    /* 6. Decode each table row */
    if (tbl_data && tbl_size >= 24) {
        int str_idx_sz = (info.heap_sizes & XX_MS_PDB_PORTABLE_HEAPSIZE_STRINGS) ? 4 : 2;
        int guid_idx_sz = (info.heap_sizes & XX_MS_PDB_PORTABLE_HEAPSIZE_GUID) ? 4 : 2;
        int blob_idx_sz = (info.heap_sizes & XX_MS_PDB_PORTABLE_HEAPSIZE_BLOB) ? 4 : 2;

        /* HasCustomDebugInformation size */
        int has_cdi_sz = 2;
        for (int k = 0; k < 27; k++) {
            uint32_t t = pdump_has_cdi_tables[k];
            if (t < 64 && info.table_rows[t] >= 2048) {
                has_cdi_sz = 4;
                break;
            }
        }

        /* Calculate table offsets */
        int bit_count = xx_ms_pdb_portable_count_bits(info.valid_tables);
        size_t tbl_offsets[64] = {0};
        size_t cur_offset = 24 + (size_t)bit_count * 4;

        for (uint32_t t = 0; t < 64; t++) {
            if (info.valid_tables & ((uint64_t)1 << t)) {
                tbl_offsets[t] = cur_offset;
                uint32_t rows = info.table_rows[t];
                int rsize = 0;
                switch (t) {
                    case 0x30: /* Document */ rsize = blob_idx_sz + guid_idx_sz + blob_idx_sz + guid_idx_sz; break;
                    case 0x31: /* MethodDebugInformation */ rsize = (info.table_rows[0x30] < 65536 ? 2 : 4) + blob_idx_sz; break;
                    case 0x32: /* LocalScope */
                        rsize = (info.table_rows[0x06] < 65536 ? 2 : 4) + (info.table_rows[0x35] < 65536 ? 2 : 4) + (info.table_rows[0x33] < 65536 ? 2 : 4) +
                                (info.table_rows[0x34] < 65536 ? 2 : 4) + 8;
                        break;
                    case 0x33: /* LocalVariable */ rsize = 4 + str_idx_sz; break;
                    case 0x34: /* LocalConstant */ rsize = str_idx_sz + blob_idx_sz; break;
                    case 0x35: /* ImportScope */ rsize = (info.table_rows[0x35] < 65536 ? 2 : 4) + blob_idx_sz; break;
                    case 0x36: /* StateMachineMethod */ rsize = (info.table_rows[0x06] < 65536 ? 2 : 4) * 2; break;
                    case 0x37: /* CustomDebugInformation */ rsize = has_cdi_sz + guid_idx_sz + blob_idx_sz; break;
                    default: rsize = 0; break;
                }
                cur_offset += (size_t)rows * (size_t)rsize;
            }
        }

        /* --- Table 0x30: Document --- */
        if (info.valid_tables & ((uint64_t)1 << 0x30)) {
            uint32_t rows = info.table_rows[0x30];
            int rsize = blob_idx_sz + guid_idx_sz + blob_idx_sz + guid_idx_sz;
            size_t base = tbl_offsets[0x30];

            pdump_begin(&ctx, "table_document", -1, -1);
            pdump_field_uint(&ctx, "rows", rows);
            for (uint32_t i = 0; i < rows; i++) {
                size_t roff = base + (size_t)i * (size_t)rsize;
                if (roff + (size_t)rsize > tbl_size) break;

                uint32_t name_idx = (blob_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)blob_idx_sz;
                uint32_t hash_alg_idx = (guid_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)guid_idx_sz;
                uint32_t hash_idx = (blob_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)blob_idx_sz;
                uint32_t lang_idx = (guid_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);

                char *doc_name = blob_data ? pdump_decode_doc_name(blob_data, blob_size, name_idx) : NULL;

                char hash_alg_guid[40] = "{00000000-0000-0000-0000-000000000000}";
                const char *hash_alg_name = "";
                if (guid_data && hash_alg_idx > 0 && (size_t)hash_alg_idx * 16 <= guid_size) {
                    const uint8_t *gbytes = guid_data + (hash_alg_idx - 1) * 16;
                    pdump_format_guid(hash_alg_guid, sizeof(hash_alg_guid), gbytes);
                    hash_alg_name = pdump_guid_name(gbytes);
                }

                char lang_guid[40] = "{00000000-0000-0000-0000-000000000000}";
                const char *lang_name = "";
                if (guid_data && lang_idx > 0 && (size_t)lang_idx * 16 <= guid_size) {
                    const uint8_t *gbytes = guid_data + (lang_idx - 1) * 16;
                    pdump_format_guid(lang_guid, sizeof(lang_guid), gbytes);
                    lang_name = pdump_guid_name(gbytes);
                }

                const uint8_t *hash_bytes = NULL;
                uint32_t hash_len = 0;
                if (blob_data && hash_idx > 0) {
                    pdump_get_blob(blob_data, blob_size, hash_idx, &hash_bytes, &hash_len);
                }

                pdump_begin(&ctx, "document", -1, -1);
                pdump_field_uint(&ctx, "row", i + 1);
                pdump_field_string(&ctx, "name", doc_name ? doc_name : "");
                pdump_field_guid(&ctx, "hash_algorithm", hash_alg_guid);
                pdump_field_enum(&ctx, "hash_algorithm_name", hash_alg_idx, hash_alg_name);
                pdump_field_bytes(&ctx, "hash", hash_bytes ? hash_bytes : (const uint8_t *)"", hash_len);
                pdump_field_guid(&ctx, "language", lang_guid);
                pdump_field_enum(&ctx, "language_name", lang_idx, lang_name);
                pdump_end(&ctx, "document", -1, -1);

                if (doc_name) xx_mem_free(doc_name);
            }
            pdump_end(&ctx, "table_document", -1, -1);
        }

        /* --- Table 0x31: MethodDebugInformation --- */
        if (info.valid_tables & ((uint64_t)1 << 0x31)) {
            uint32_t rows = info.table_rows[0x31];
            int doc_ref_sz = (info.table_rows[0x30] < 65536) ? 2 : 4;
            int rsize = doc_ref_sz + blob_idx_sz;
            size_t base = tbl_offsets[0x31];

            pdump_begin(&ctx, "table_method_debug_information", -1, -1);
            pdump_field_uint(&ctx, "rows", rows);
            for (uint32_t i = 0; i < rows; i++) {
                size_t roff = base + (size_t)i * (size_t)rsize;
                if (roff + (size_t)rsize > tbl_size) break;

                uint32_t doc_ref = (doc_ref_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)doc_ref_sz;
                uint32_t sp_blob_idx = (blob_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);

                pdump_begin(&ctx, "method_debug_information", -1, -1);
                pdump_field_uint(&ctx, "row", i + 1);
                pdump_field_uint(&ctx, "document", doc_ref);
                pdump_field_uint(&ctx, "sequence_points_blob_index", sp_blob_idx);

                /* Decode sequence points blob */
                const uint8_t *sp_payload = NULL;
                uint32_t sp_len = 0;
                if (blob_data && sp_blob_idx > 0 && pdump_get_blob(blob_data, blob_size, sp_blob_idx, &sp_payload, &sp_len) && sp_len > 0) {
                    size_t sp_off = 0;
                    uint32_t local_sig = 0;
                    int32_t lb = pdump_read_compressed_uint(sp_payload, sp_len, sp_off, &local_sig);
                    if (lb > 0) {
                        sp_off += (size_t)lb;
                        uint32_t cur_doc = doc_ref;
                        if (cur_doc == 0) {
                            lb = pdump_read_compressed_uint(sp_payload, sp_len, sp_off, &cur_doc);
                            if (lb > 0) sp_off += (size_t)lb;
                        }

                        int32_t prev_non_hidden_start_line = -1;
                        int32_t prev_non_hidden_start_col = -1;
                        uint32_t prev_il_offset = 0;
                        bool is_first = true;

                        while (sp_off < sp_len) {
                            uint32_t il_delta = 0;
                            lb = pdump_read_compressed_uint(sp_payload, sp_len, sp_off, &il_delta);
                            if (lb <= 0) break;
                            sp_off += (size_t)lb;

                            while (!is_first && il_delta == 0) {
                                lb = pdump_read_compressed_uint(sp_payload, sp_len, sp_off, &cur_doc);
                                if (lb <= 0) break;
                                sp_off += (size_t)lb;
                                if (sp_off >= sp_len) break;
                                lb = pdump_read_compressed_uint(sp_payload, sp_len, sp_off, &il_delta);
                                if (lb <= 0) break;
                                sp_off += (size_t)lb;
                            }
                            if (lb <= 0) break;

                            uint32_t il_offset = is_first ? il_delta : (prev_il_offset + il_delta);
                            prev_il_offset = il_offset;
                            is_first = false;

                            uint32_t delta_lines = 0;
                            lb = pdump_read_compressed_uint(sp_payload, sp_len, sp_off, &delta_lines);
                            if (lb <= 0) break;
                            sp_off += (size_t)lb;

                            int32_t delta_cols = 0;
                            if (delta_lines != 0) {
                                lb = pdump_read_compressed_int(sp_payload, sp_len, sp_off, &delta_cols);
                            } else {
                                uint32_t u_cols = 0;
                                lb = pdump_read_compressed_uint(sp_payload, sp_len, sp_off, &u_cols);
                                delta_cols = (int32_t)u_cols;
                            }
                            if (lb <= 0) break;
                            sp_off += (size_t)lb;

                            uint32_t start_line = 0, end_line = 0;
                            uint32_t start_col = 0, end_col = 0;
                            bool is_hidden = false;

                            if (delta_lines == 0 && delta_cols == 0) {
                                is_hidden = true;
                                start_line = 0xFEEFEE;
                                end_line = 0xFEEFEE;
                                start_col = 0;
                                end_col = 0;
                            } else {
                                if (prev_non_hidden_start_line < 0) {
                                    uint32_t u_line = 0, u_col = 0;
                                    lb = pdump_read_compressed_uint(sp_payload, sp_len, sp_off, &u_line);
                                    if (lb <= 0) break;
                                    sp_off += (size_t)lb;
                                    lb = pdump_read_compressed_uint(sp_payload, sp_len, sp_off, &u_col);
                                    if (lb <= 0) break;
                                    sp_off += (size_t)lb;
                                    start_line = u_line;
                                    start_col = u_col;
                                } else {
                                    int32_t diff_line = 0, diff_col = 0;
                                    lb = pdump_read_compressed_int(sp_payload, sp_len, sp_off, &diff_line);
                                    if (lb <= 0) break;
                                    sp_off += (size_t)lb;
                                    lb = pdump_read_compressed_int(sp_payload, sp_len, sp_off, &diff_col);
                                    if (lb <= 0) break;
                                    sp_off += (size_t)lb;
                                    start_line = (uint32_t)((int32_t)prev_non_hidden_start_line + diff_line);
                                    start_col = (uint32_t)((int32_t)prev_non_hidden_start_col + diff_col);
                                }
                                prev_non_hidden_start_line = (int32_t)start_line;
                                prev_non_hidden_start_col = (int32_t)start_col;
                                end_line = start_line + delta_lines;
                                end_col = (uint32_t)((int32_t)start_col + delta_cols);
                            }

                            pdump_begin(&ctx, "sequence_point", -1, -1);
                            pdump_field_hex(&ctx, "offset", il_offset);
                            pdump_field_uint(&ctx, "start_line", start_line);
                            pdump_field_uint(&ctx, "end_line", end_line);
                            pdump_field_uint(&ctx, "start_column", start_col);
                            pdump_field_uint(&ctx, "end_column", end_col);
                            pdump_field_bool(&ctx, "is_hidden", is_hidden);
                            pdump_field_uint(&ctx, "document", cur_doc);
                            pdump_end(&ctx, "sequence_point", -1, -1);
                        }
                    }
                }

                pdump_end(&ctx, "method_debug_information", -1, -1);
            }
            pdump_end(&ctx, "table_method_debug_information", -1, -1);
        }

        /* --- Table 0x32: LocalScope --- */
        if (info.valid_tables & ((uint64_t)1 << 0x32)) {
            uint32_t rows = info.table_rows[0x32];
            int m_sz = (info.table_rows[0x06] < 65536) ? 2 : 4;
            int is_sz = (info.table_rows[0x35] < 65536) ? 2 : 4;
            int vl_sz = (info.table_rows[0x33] < 65536) ? 2 : 4;
            int cl_sz = (info.table_rows[0x34] < 65536) ? 2 : 4;
            int rsize = m_sz + is_sz + vl_sz + cl_sz + 8;
            size_t base = tbl_offsets[0x32];

            pdump_begin(&ctx, "table_local_scope", -1, -1);
            pdump_field_uint(&ctx, "rows", rows);
            for (uint32_t i = 0; i < rows; i++) {
                size_t roff = base + (size_t)i * (size_t)rsize;
                if (roff + (size_t)rsize > tbl_size) break;

                uint32_t method_row = (m_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)m_sz;
                uint32_t iscope_row = (is_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)is_sz;
                uint32_t var_list = (vl_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)vl_sz;
                uint32_t const_list = (cl_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)cl_sz;
                uint32_t start_off = pdump_u32(tbl_data, tbl_size, roff);
                roff += 4;
                uint32_t length = pdump_u32(tbl_data, tbl_size, roff);

                uint32_t vars_count = 0;
                uint32_t next_var = 0;
                if (i + 1 < rows) {
                    size_t next_roff = base + (size_t)(i + 1) * (size_t)rsize + (size_t)m_sz + (size_t)is_sz;
                    if (next_roff + (size_t)vl_sz <= tbl_size) {
                        next_var = (vl_sz == 2) ? pdump_u16(tbl_data, tbl_size, next_roff) : pdump_u32(tbl_data, tbl_size, next_roff);
                    }
                } else {
                    next_var = info.table_rows[0x33] + 1;
                }
                if (next_var >= var_list) vars_count = next_var - var_list;

                uint32_t consts_count = 0;
                uint32_t next_const = 0;
                if (i + 1 < rows) {
                    size_t next_roff = base + (size_t)(i + 1) * (size_t)rsize + (size_t)m_sz + (size_t)is_sz + (size_t)vl_sz;
                    if (next_roff + (size_t)cl_sz <= tbl_size) {
                        next_const = (cl_sz == 2) ? pdump_u16(tbl_data, tbl_size, next_roff) : pdump_u32(tbl_data, tbl_size, next_roff);
                    }
                } else {
                    next_const = info.table_rows[0x34] + 1;
                }
                if (next_const >= const_list) consts_count = next_const - const_list;

                uint32_t method_token = (0x06 << 24) | method_row;

                pdump_begin(&ctx, "local_scope", -1, -1);
                pdump_field_uint(&ctx, "row", i + 1);
                pdump_field_hex(&ctx, "method", method_token);
                pdump_field_uint(&ctx, "import_scope", iscope_row);
                pdump_field_uint(&ctx, "variable_list", var_list);
                pdump_field_uint(&ctx, "constant_list", const_list);
                pdump_field_hex(&ctx, "start_offset", start_off);
                pdump_field_hex(&ctx, "length", length);
                pdump_field_uint(&ctx, "vars_count", vars_count);
                pdump_field_uint(&ctx, "consts_count", consts_count);
                pdump_end(&ctx, "local_scope", -1, -1);
            }
            pdump_end(&ctx, "table_local_scope", -1, -1);
        }

        /* --- Table 0x33: LocalVariable --- */
        if (info.valid_tables & ((uint64_t)1 << 0x33)) {
            uint32_t rows = info.table_rows[0x33];
            int rsize = 4 + str_idx_sz;
            size_t base = tbl_offsets[0x33];

            pdump_begin(&ctx, "table_local_variable", -1, -1);
            pdump_field_uint(&ctx, "rows", rows);
            for (uint32_t i = 0; i < rows; i++) {
                size_t roff = base + (size_t)i * (size_t)rsize;
                if (roff + (size_t)rsize > tbl_size) break;

                uint16_t attrs = pdump_u16(tbl_data, tbl_size, roff);
                uint16_t slot = pdump_u16(tbl_data, tbl_size, roff + 2);
                uint32_t name_idx = (str_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff + 4) : pdump_u32(tbl_data, tbl_size, roff + 4);

                const char *var_name = "";
                if (str_data && (size_t)name_idx < str_size) {
                    var_name = (const char *)(str_data + name_idx);
                }

                pdump_begin(&ctx, "local_variable", -1, -1);
                pdump_field_uint(&ctx, "row", i + 1);
                pdump_field_hex(&ctx, "attributes", attrs);
                pdump_field_uint(&ctx, "index", slot);
                pdump_field_string(&ctx, "name", var_name);
                pdump_end(&ctx, "local_variable", -1, -1);
            }
            pdump_end(&ctx, "table_local_variable", -1, -1);
        }

        /* --- Table 0x34: LocalConstant --- */
        if (info.valid_tables & ((uint64_t)1 << 0x34)) {
            uint32_t rows = info.table_rows[0x34];
            int rsize = str_idx_sz + blob_idx_sz;
            size_t base = tbl_offsets[0x34];

            pdump_begin(&ctx, "table_local_constant", -1, -1);
            pdump_field_uint(&ctx, "rows", rows);
            for (uint32_t i = 0; i < rows; i++) {
                size_t roff = base + (size_t)i * (size_t)rsize;
                if (roff + (size_t)rsize > tbl_size) break;

                uint32_t name_idx = (str_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)str_idx_sz;
                uint32_t sig_idx = (blob_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);

                const char *const_name = "";
                if (str_data && (size_t)name_idx < str_size) {
                    const_name = (const char *)(str_data + name_idx);
                }

                const uint8_t *sig_bytes = NULL;
                uint32_t sig_len = 0;
                if (blob_data && sig_idx > 0) {
                    pdump_get_blob(blob_data, blob_size, sig_idx, &sig_bytes, &sig_len);
                }

                pdump_begin(&ctx, "local_constant", -1, -1);
                pdump_field_uint(&ctx, "row", i + 1);
                pdump_field_string(&ctx, "name", const_name);
                pdump_field_bytes(&ctx, "signature", sig_bytes ? sig_bytes : (const uint8_t *)"", sig_len);

                if (sig_bytes && sig_len > 0) {
                    uint8_t et = sig_bytes[0];
                    pdump_field_enum(&ctx, "type", et, pdump_element_type_name(et));
                }

                pdump_end(&ctx, "local_constant", -1, -1);
            }
            pdump_end(&ctx, "table_local_constant", -1, -1);
        }

        /* --- Table 0x35: ImportScope --- */
        if (info.valid_tables & ((uint64_t)1 << 0x35)) {
            uint32_t rows = info.table_rows[0x35];
            int is_sz = (info.table_rows[0x35] < 65536) ? 2 : 4;
            int rsize = is_sz + blob_idx_sz;
            size_t base = tbl_offsets[0x35];

            pdump_begin(&ctx, "table_import_scope", -1, -1);
            pdump_field_uint(&ctx, "rows", rows);
            for (uint32_t i = 0; i < rows; i++) {
                size_t roff = base + (size_t)i * (size_t)rsize;
                if (roff + (size_t)rsize > tbl_size) break;

                uint32_t parent = (is_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)is_sz;
                uint32_t imp_blob_idx = (blob_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);

                pdump_begin(&ctx, "import_scope", -1, -1);
                pdump_field_uint(&ctx, "row", i + 1);
                pdump_field_uint(&ctx, "parent", parent);

                const uint8_t *imp_payload = NULL;
                uint32_t imp_len = 0;
                if (blob_data && imp_blob_idx > 0 && pdump_get_blob(blob_data, blob_size, imp_blob_idx, &imp_payload, &imp_len) && imp_len > 0) {
                    size_t off = 0;
                    while (off < imp_len) {
                        uint8_t kind_byte = imp_payload[off++];
                        uint32_t kind = (uint32_t)kind_byte;
                        uint32_t alias_idx = 0;
                        uint32_t target_assembly_row = 0;
                        uint32_t target_type_token = 0;
                        uint32_t target_ns_idx = 0;

                        switch (kind) {
                            case 1: /* ImportNamespace */ off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &target_ns_idx); break;
                            case 2: /* ImportAssemblyNamespace */
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &target_assembly_row);
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &target_ns_idx);
                                break;
                            case 3: /* ImportType */
                            {
                                uint32_t coded = 0;
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &coded);
                                uint32_t tag = coded & 3;
                                uint32_t r = coded >> 2;
                                uint32_t tbl = (tag == 0 ? 0x02 : (tag == 1 ? 0x01 : 0x1B));
                                target_type_token = (tbl << 24) | r;
                            } break;
                            case 4: /* ImportXmlNamespace */
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &alias_idx);
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &target_ns_idx);
                                break;
                            case 5: /* ImportAssemblyReferenceAlias */ off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &alias_idx); break;
                            case 6: /* AliasAssemblyReference */
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &alias_idx);
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &target_assembly_row);
                                break;
                            case 7: /* AliasNamespace */
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &alias_idx);
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &target_ns_idx);
                                break;
                            case 8: /* AliasAssemblyNamespace */
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &alias_idx);
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &target_assembly_row);
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &target_ns_idx);
                                break;
                            case 9: /* AliasType */
                            {
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &alias_idx);
                                uint32_t coded = 0;
                                off += (size_t)pdump_read_compressed_uint(imp_payload, imp_len, off, &coded);
                                uint32_t tag = coded & 3;
                                uint32_t r = coded >> 2;
                                uint32_t tbl = (tag == 0 ? 0x02 : (tag == 1 ? 0x01 : 0x1B));
                                target_type_token = (tbl << 24) | r;
                            } break;
                            default: off = imp_len; break;
                        }

                        /* Extract string values for alias and namespace from blob heap */
                        char alias_buf[256] = "";
                        if (alias_idx > 0 && blob_data) {
                            const uint8_t *ap = NULL;
                            uint32_t al = 0;
                            if (pdump_get_blob(blob_data, blob_size, alias_idx, &ap, &al) && ap && al > 0) {
                                size_t cplen = al < 255 ? al : 255;
                                xx_mem_copy(alias_buf, ap, cplen);
                                alias_buf[cplen] = '\0';
                            }
                        }

                        char ns_buf[512] = "";
                        if (target_ns_idx > 0 && blob_data) {
                            const uint8_t *np = NULL;
                            uint32_t nl = 0;
                            if (pdump_get_blob(blob_data, blob_size, target_ns_idx, &np, &nl) && np && nl > 0) {
                                size_t cplen = nl < 511 ? nl : 511;
                                xx_mem_copy(ns_buf, np, cplen);
                                ns_buf[cplen] = '\0';
                            }
                        }

                        uint32_t target_assembly_token = (target_assembly_row > 0) ? ((0x23 << 24) | target_assembly_row) : 0;

                        pdump_begin(&ctx, "import", -1, -1);
                        pdump_field_enum(&ctx, "kind", kind, pdump_import_kind_name(kind));
                        pdump_field_string(&ctx, "alias", alias_buf);
                        pdump_field_hex(&ctx, "target_assembly", target_assembly_token);
                        pdump_field_hex(&ctx, "target_type", target_type_token);
                        pdump_field_string(&ctx, "target_namespace", ns_buf);
                        pdump_end(&ctx, "import", -1, -1);
                    }
                }

                pdump_end(&ctx, "import_scope", -1, -1);
            }
            pdump_end(&ctx, "table_import_scope", -1, -1);
        }

        /* --- Table 0x36: StateMachineMethod --- */
        if (info.valid_tables & ((uint64_t)1 << 0x36)) {
            uint32_t rows = info.table_rows[0x36];
            int m_sz = (info.table_rows[0x06] < 65536) ? 2 : 4;
            int rsize = m_sz * 2;
            size_t base = tbl_offsets[0x36];

            pdump_begin(&ctx, "table_state_machine_method", -1, -1);
            pdump_field_uint(&ctx, "rows", rows);
            for (uint32_t i = 0; i < rows; i++) {
                size_t roff = base + (size_t)i * (size_t)rsize;
                if (roff + (size_t)rsize > tbl_size) break;

                uint32_t move_next_row = (m_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                uint32_t kickoff_row = (m_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff + m_sz) : pdump_u32(tbl_data, tbl_size, roff + m_sz);

                uint32_t move_next_token = (0x06 << 24) | move_next_row;
                uint32_t kickoff_token = (0x06 << 24) | kickoff_row;

                pdump_begin(&ctx, "state_machine_method", -1, -1);
                pdump_field_uint(&ctx, "row", i + 1);
                pdump_field_hex(&ctx, "move_next_method", move_next_token);
                pdump_field_hex(&ctx, "kickoff_method", kickoff_token);
                pdump_end(&ctx, "state_machine_method", -1, -1);
            }
            pdump_end(&ctx, "table_state_machine_method", -1, -1);
        }

        /* --- Table 0x37: CustomDebugInformation --- */
        if (info.valid_tables & ((uint64_t)1 << 0x37)) {
            uint32_t rows = info.table_rows[0x37];
            int rsize = has_cdi_sz + guid_idx_sz + blob_idx_sz;
            size_t base = tbl_offsets[0x37];

            pdump_begin(&ctx, "table_custom_debug_information", -1, -1);
            pdump_field_uint(&ctx, "rows", rows);
            for (uint32_t i = 0; i < rows; i++) {
                size_t roff = base + (size_t)i * (size_t)rsize;
                if (roff + (size_t)rsize > tbl_size) break;

                uint32_t parent_coded = (has_cdi_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)has_cdi_sz;
                uint32_t kind_idx = (guid_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
                roff += (size_t)guid_idx_sz;
                uint32_t val_idx = (blob_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);

                uint32_t parent_token = pdump_decode_has_cdi(parent_coded);

                char kind_guid[40] = "{00000000-0000-0000-0000-000000000000}";
                const char *kind_name = "";
                const uint8_t *kind_raw = NULL;
                if (guid_data && kind_idx > 0 && (size_t)kind_idx * 16 <= guid_size) {
                    kind_raw = guid_data + (kind_idx - 1) * 16;
                    pdump_format_guid(kind_guid, sizeof(kind_guid), kind_raw);
                    kind_name = pdump_guid_name(kind_raw);
                }

                const uint8_t *val_bytes = NULL;
                uint32_t val_len = 0;
                if (blob_data && val_idx > 0) {
                    pdump_get_blob(blob_data, blob_size, val_idx, &val_bytes, &val_len);
                }

                pdump_begin(&ctx, "custom_debug_information", -1, -1);
                pdump_field_uint(&ctx, "row", i + 1);
                pdump_field_hex(&ctx, "parent", parent_token);
                pdump_field_guid(&ctx, "kind", kind_guid);
                pdump_field_enum(&ctx, "kind_name", kind_idx, kind_name);
                pdump_field_uint(&ctx, "length", val_len);

                /* Kind-specific decoders */
                if (val_bytes && val_len > 0) {
                    if (strcmp(kind_name, "SourceLink") == 0) {
                        /* UTF-8 JSON text */
                        char *json_txt = (char *)xx_mem_alloc((size_t)val_len + 1);
                        if (json_txt) {
                            xx_mem_copy(json_txt, val_bytes, val_len);
                            json_txt[val_len] = '\0';
                            pdump_field_string(&ctx, "text", json_txt);
                            xx_mem_free(json_txt);
                        }
                    } else if (strcmp(kind_name, "EmbeddedSource") == 0 && val_len >= 4) {
                        int32_t uncomp_sz = pdump_i32(val_bytes, val_len, 0);
                        pdump_field_int(&ctx, "uncompressed_size", uncomp_sz);
                        if (uncomp_sz > 0) {
                            uint8_t *uncomp = (uint8_t *)xx_mem_alloc((size_t)uncomp_sz + 1);
                            if (uncomp) {
                                size_t written = 0;
                                if (xx_deflate_decompress_memory(val_bytes + 4, val_len - 4, uncomp, (size_t)uncomp_sz, &written, false)) {
                                    pdump_field_bytes(&ctx, "data", uncomp, written);
                                    /* If first 64 KiB looks like text, emit as source_text */
                                    size_t str_check = written < 65536 ? written : 65536;
                                    uncomp[str_check] = '\0';
                                    pdump_field_string(&ctx, "source_text", (const char *)uncomp);
                                }
                                xx_mem_free(uncomp);
                            }
                        } else {
                            pdump_field_bytes(&ctx, "data", val_bytes + 4, val_len - 4);
                        }
                    } else if (strcmp(kind_name, "CompilationOptions") == 0) {
                        size_t c_off = 0;
                        while (c_off < val_len) {
                            size_t k_start = c_off;
                            while (c_off < val_len && val_bytes[c_off] != '\0') c_off++;
                            if (c_off >= val_len) break;
                            const char *k_str = (const char *)(val_bytes + k_start);
                            c_off++; /* skip null */

                            size_t v_start = c_off;
                            while (c_off < val_len && val_bytes[c_off] != '\0') c_off++;
                            const char *v_str = (const char *)(val_bytes + v_start);
                            if (c_off < val_len) c_off++;

                            pdump_begin(&ctx, "compilation_option", -1, -1);
                            pdump_field_string(&ctx, "key", k_str);
                            pdump_field_string(&ctx, "value", v_str);
                            pdump_end(&ctx, "compilation_option", -1, -1);
                        }
                    } else if (strcmp(kind_name, "CompilationMetadataReferences") == 0) {
                        size_t r_off = 0;
                        while (r_off < val_len) {
                            size_t t_start = r_off;
                            while (r_off < val_len && val_bytes[r_off] != '\0') r_off++;
                            if (r_off >= val_len) break;
                            const char *t_str = (const char *)(val_bytes + t_start);
                            r_off++;

                            size_t a_start = r_off;
                            while (r_off < val_len && val_bytes[r_off] != '\0') r_off++;
                            if (r_off >= val_len) break;
                            const char *a_str = (const char *)(val_bytes + a_start);
                            r_off++;

                            if (r_off + 25 > val_len) break;

                            uint8_t flags = val_bytes[r_off++];
                            uint32_t stamp = pdump_u32(val_bytes, val_len, r_off);
                            r_off += 4;
                            uint32_t img_sz = pdump_u32(val_bytes, val_len, r_off);
                            r_off += 4;
                            char mvid[40];
                            pdump_format_guid(mvid, sizeof(mvid), val_bytes + r_off);
                            r_off += 16;

                            pdump_begin(&ctx, "compilation_metadata_reference", -1, -1);
                            pdump_field_string(&ctx, "title", t_str);
                            pdump_field_string(&ctx, "aliases", a_str);
                            pdump_field_hex(&ctx, "flags", flags);
                            pdump_field_hex(&ctx, "timestamp", stamp);
                            pdump_field_uint(&ctx, "image_size", img_sz);
                            pdump_field_guid(&ctx, "mvid", mvid);
                            pdump_end(&ctx, "compilation_metadata_reference", -1, -1);
                        }
                    } else {
                        pdump_field_bytes(&ctx, "data", val_bytes, val_len);
                    }
                }

                pdump_end(&ctx, "custom_debug_information", -1, -1);
            }
            pdump_end(&ctx, "table_custom_debug_information", -1, -1);
        }
    }

    if (tbl_data) xx_mem_free(tbl_data);
    if (str_data) xx_mem_free(str_data);
    if (blob_data) xx_mem_free(blob_data);
    if (guid_data) xx_mem_free(guid_data);
    xx_ms_pdb_portable_details_free(&info);

    return !ctx.stopped;
}

/* ---- Public API: xx_ms_pdb_portable_embedded_sources ------------------- */

XXFC_API bool xx_ms_pdb_portable_embedded_sources(xx_io_device *device, xx_ms_pdb_portable_source_fn fn, void *user)
{
    if (!device || !fn) return false;

    xx_ms_pdb_portable_details info = {0};
    if (!xx_ms_pdb_portable_read_info(device, &info)) {
        return false;
    }

    if (!info.tables_valid || (info.valid_tables & ((uint64_t)1 << 0x37)) == 0) {
        xx_ms_pdb_portable_details_free(&info);
        return true;
    }

    const xx_ms_pdb_portable_stream *tbl_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_TABLES);
    if (!tbl_stream) {
        tbl_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_TABLES_UNCOMPRESSED);
    }
    const xx_ms_pdb_portable_stream *blob_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_BLOB);
    const xx_ms_pdb_portable_stream *guid_stream = xx_ms_pdb_portable_get_stream(&info, XX_MS_PDB_PORTABLE_STREAM_GUID);

    if (!tbl_stream || !blob_stream || !guid_stream || tbl_stream->size == 0 || blob_stream->size == 0 || guid_stream->size == 0 ||
        tbl_stream->size > PDUMP_MAX_STREAM_SIZE || blob_stream->size > PDUMP_MAX_STREAM_SIZE || guid_stream->size > PDUMP_MAX_STREAM_SIZE) {
        xx_ms_pdb_portable_details_free(&info);
        return true;
    }

    uint8_t *tbl_data = (uint8_t *)xx_mem_alloc(tbl_stream->size);
    uint8_t *blob_data = (uint8_t *)xx_mem_alloc(blob_stream->size);
    uint8_t *guid_data = (uint8_t *)xx_mem_alloc(guid_stream->size);

    if (!tbl_data || !blob_data || !guid_data) {
        if (tbl_data) xx_mem_free(tbl_data);
        if (blob_data) xx_mem_free(blob_data);
        if (guid_data) xx_mem_free(guid_data);
        xx_ms_pdb_portable_details_free(&info);
        return false;
    }

    size_t tbl_size = 0, blob_size = 0, guid_size = 0;
    if (xx_io_seek64(device, tbl_stream->offset, SEEK_SET) == 0) {
        ssize_t n = xx_io_read(device, tbl_data, tbl_stream->size);
        if (n > 0) tbl_size = (size_t)n;
    }
    if (xx_io_seek64(device, blob_stream->offset, SEEK_SET) == 0) {
        ssize_t n = xx_io_read(device, blob_data, blob_stream->size);
        if (n > 0) blob_size = (size_t)n;
    }
    if (xx_io_seek64(device, guid_stream->offset, SEEK_SET) == 0) {
        ssize_t n = xx_io_read(device, guid_data, guid_stream->size);
        if (n > 0) guid_size = (size_t)n;
    }

    int str_idx_sz = (info.heap_sizes & XX_MS_PDB_PORTABLE_HEAPSIZE_STRINGS) ? 4 : 2;
    int guid_idx_sz = (info.heap_sizes & XX_MS_PDB_PORTABLE_HEAPSIZE_GUID) ? 4 : 2;
    int blob_idx_sz = (info.heap_sizes & XX_MS_PDB_PORTABLE_HEAPSIZE_BLOB) ? 4 : 2;

    int has_cdi_sz = 2;
    for (int k = 0; k < 27; k++) {
        uint32_t t = pdump_has_cdi_tables[k];
        if (t < 64 && info.table_rows[t] >= 2048) {
            has_cdi_sz = 4;
            break;
        }
    }

    int bit_count = xx_ms_pdb_portable_count_bits(info.valid_tables);
    size_t tbl_offsets[64] = {0};
    size_t cur_offset = 24 + (size_t)bit_count * 4;

    for (uint32_t t = 0; t < 64; t++) {
        if (info.valid_tables & ((uint64_t)1 << t)) {
            tbl_offsets[t] = cur_offset;
            uint32_t rows = info.table_rows[t];
            int rsize = 0;
            switch (t) {
                case 0x30: rsize = blob_idx_sz + guid_idx_sz + blob_idx_sz + guid_idx_sz; break;
                case 0x31: rsize = (info.table_rows[0x30] < 65536 ? 2 : 4) + blob_idx_sz; break;
                case 0x32:
                    rsize = (info.table_rows[0x06] < 65536 ? 2 : 4) + (info.table_rows[0x35] < 65536 ? 2 : 4) + (info.table_rows[0x33] < 65536 ? 2 : 4) +
                            (info.table_rows[0x34] < 65536 ? 2 : 4) + 8;
                    break;
                case 0x33: rsize = 4 + str_idx_sz; break;
                case 0x34: rsize = str_idx_sz + blob_idx_sz; break;
                case 0x35: rsize = (info.table_rows[0x35] < 65536 ? 2 : 4) + blob_idx_sz; break;
                case 0x36: rsize = (info.table_rows[0x06] < 65536 ? 2 : 4) * 2; break;
                case 0x37: rsize = has_cdi_sz + guid_idx_sz + blob_idx_sz; break;
                default: rsize = 0; break;
            }
            cur_offset += (size_t)rows * (size_t)rsize;
        }
    }

    /* EmbeddedSource GUID: 0E8A571B-6926-466E-B4AD-8AB04611F5FE */
    static const uint8_t emb_guid[16] = {0x1B, 0x57, 0x8A, 0x0E, 0x26, 0x69, 0x6E, 0x46, 0xB4, 0xAD, 0x8A, 0xB0, 0x46, 0x11, 0xF5, 0xFE};

    uint32_t cdi_rows = info.table_rows[0x37];
    int cdi_rsize = has_cdi_sz + guid_idx_sz + blob_idx_sz;
    size_t cdi_base = tbl_offsets[0x37];

    bool cont = true;
    for (uint32_t i = 0; i < cdi_rows && cont; i++) {
        size_t roff = cdi_base + (size_t)i * (size_t)cdi_rsize;
        if (roff + (size_t)cdi_rsize > tbl_size) break;

        uint32_t parent_coded = (has_cdi_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
        roff += (size_t)has_cdi_sz;
        uint32_t kind_idx = (guid_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);
        roff += (size_t)guid_idx_sz;
        uint32_t val_idx = (blob_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, roff) : pdump_u32(tbl_data, tbl_size, roff);

        if (kind_idx == 0 || (size_t)kind_idx * 16 > guid_size) continue;
        const uint8_t *gbytes = guid_data + (kind_idx - 1) * 16;
        if (memcmp(gbytes, emb_guid, 16) != 0) continue;

        /* Must point to Document (tag 22 in HasCustomDebugInformation) */
        uint32_t tag = parent_coded & 0x1F;
        uint32_t doc_row = parent_coded >> 5;
        if (tag != 22 || doc_row == 0 || doc_row > info.table_rows[0x30]) continue;

        /* Resolve document name from Document table row `doc_row - 1` */
        int doc_rsize = blob_idx_sz + guid_idx_sz + blob_idx_sz + guid_idx_sz;
        size_t doc_roff = tbl_offsets[0x30] + (size_t)(doc_row - 1) * (size_t)doc_rsize;
        if (doc_roff + (size_t)blob_idx_sz > tbl_size) continue;

        uint32_t name_idx = (blob_idx_sz == 2) ? pdump_u16(tbl_data, tbl_size, doc_roff) : pdump_u32(tbl_data, tbl_size, doc_roff);
        char *doc_name = pdump_decode_doc_name(blob_data, blob_size, name_idx);

        /* Read EmbeddedSource value blob */
        const uint8_t *val_bytes = NULL;
        uint32_t val_len = 0;
        if (pdump_get_blob(blob_data, blob_size, val_idx, &val_bytes, &val_len) && val_bytes && val_len >= 4) {
            int32_t uncomp_sz = pdump_i32(val_bytes, val_len, 0);
            if (uncomp_sz > 0) {
                uint8_t *uncomp = (uint8_t *)xx_mem_alloc((size_t)uncomp_sz + 1);
                if (uncomp) {
                    size_t written = 0;
                    if (xx_deflate_decompress_memory(val_bytes + 4, val_len - 4, uncomp, (size_t)uncomp_sz, &written, false)) {
                        uncomp[written] = '\0';
                        if (!fn(user, doc_name ? doc_name : "", uncomp, written)) {
                            cont = false;
                        }
                    }
                    xx_mem_free(uncomp);
                }
            } else if (uncomp_sz == 0) {
                /* Not compressed: raw bytes follow */
                if (!fn(user, doc_name ? doc_name : "", val_bytes + 4, val_len - 4)) {
                    cont = false;
                }
            }
        }

        if (doc_name) xx_mem_free(doc_name);
    }

    xx_mem_free(tbl_data);
    xx_mem_free(blob_data);
    xx_mem_free(guid_data);
    xx_ms_pdb_portable_details_free(&info);

    return cont;
}
