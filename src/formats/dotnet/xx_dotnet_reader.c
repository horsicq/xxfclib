/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/dotnet/xx_dotnet_reader.h"
#include "../xx_executable_inspect_internal.h"

#define DOTNET_READER_MAX_CODE (16U * 1024U * 1024U)
#define DOTNET_READER_MAX_SECTIONS 64U
#define DOTNET_READER_MAX_CLAUSES 4096U

static bool reader_begin(xx_dotnet_inspection *state) {
    if (!state || !state->pe.pInput || !state->pe.pInput->device || !state->cli.bValid) return false;
    state->pe.pInput->read_work = 0;
    state->pe.pInput->failed = false;
    return state->pe.pInput->size >= 0 && !xx_pd_is_stopped(state->pe.pInput->pd);
}
static bool reader_range(int64_t limit, int64_t offset, uint64_t size) {
    return limit >= 0 && offset >= 0 && offset <= limit && size <= (uint64_t)(limit - offset);
}
static bool reader_row(xx_dotnet_inspection *state, unsigned table, uint32_t rid,
    int64_t *offset, uint32_t *size) {
    const xx_dotnet_inspect_cli *cli = &state->cli;
    uint64_t extent, delta; int64_t start;
    if (table > XX_DOTNET_MDT_GenericParamConstraint || !rid || rid > cli->pRows[table] ||
        cli->pRows[table] > 0xffffffU || cli->pElementSize[table] <= 0 ||
        !reader_range(state->pe.pInput->size, cli->nTablesOffset, (uint64_t)cli->nTablesSize)) return false;
    start = cli->pTableOffset[table];
    if (start < cli->nTablesOffset) return false;
    extent = (uint64_t)cli->pRows[table] * (uint32_t)cli->pElementSize[table];
    if (!reader_range(cli->nTablesSize, start - cli->nTablesOffset, extent) ||
        !reader_range(state->pe.pInput->size, start, extent)) return false;
    delta = (uint64_t)(rid - 1) * (uint32_t)cli->pElementSize[table];
    *offset = start + (int64_t)delta; *size = (uint32_t)cli->pElementSize[table]; return true;
}
bool xx_dotnet_inspect_table_row(xx_dotnet_inspection *state, unsigned table,
    uint32_t rid, int64_t *offset, uint32_t *size) {
    if (offset) *offset = 0; if (size) *size = 0;
    return reader_begin(state) && offset && size && reader_row(state, table, rid, offset, size);
}

bool xx_dotnet_inspect_blob(xx_dotnet_inspection *state, uint32_t index,
    int64_t *offset, uint32_t *size) {
    int64_t start, available; uint8_t prefix[4]; unsigned width; uint32_t length;
    if (offset) *offset = 0; if (size) *size = 0;
    if (!reader_begin(state) || !offset || !size || state->cli.nBlobSize <= 0 ||
        !reader_range(state->pe.pInput->size, state->cli.nBlobOffset, (uint64_t)state->cli.nBlobSize) ||
        index >= (uint64_t)state->cli.nBlobSize) return false;
    start = state->cli.nBlobOffset + index; available = state->cli.nBlobSize - index;
    if (!xx_exec_read(state->pe.pInput, start, prefix, 1)) return false;
    if (!index) { if (prefix[0]) return false; *offset = start; return true; }
    if (!(prefix[0] & 0x80)) { width = 1; length = prefix[0]; }
    else if ((prefix[0] & 0xc0) == 0x80) {
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
    *offset = start + width; *size = length; return true;
}

/* The header and all extra sections must stay in the same original raw
 * backing range. A truncated raw tail reduces availability, never exposes
 * a following section, overlay or virtual zero-filled memory. */
static bool reader_raw_range(xx_dotnet_inspection *state, uint32_t rva,
    int64_t *offset, uint64_t *available) {
    const xx_pe_inspection *pe = &state->pe; int i; uint64_t raw, count;
    if (!rva || !pe->bValid || pe->nSectionCount < 0 || pe->nSectionCount > 4096 ||
        (pe->nSectionCount && !pe->pSections)) return false;
    if (rva < pe->nSizeOfHeaders && rva < (uint64_t)pe->pInput->size) {
        *offset = rva; count = (uint64_t)pe->nSizeOfHeaders - rva;
        *available = count < (uint64_t)(pe->pInput->size - rva) ? count : (uint64_t)(pe->pInput->size - rva); return true;
    }
    for (i = 0; i < pe->nSectionCount; ++i) {
        const xx_pe_inspect_section *section = &pe->pSections[i]; uint64_t delta;
        if (rva < section->nVirtualAddress) continue;
        delta = (uint64_t)rva - section->nVirtualAddress;
        if (delta >= section->nSizeOfRawData) continue;
        raw = (uint64_t)section->nPointerToRawData + delta;
        if (raw >= (uint64_t)pe->pInput->size) return false;
        count = section->nSizeOfRawData - delta;
        if (count > UINT64_C(0x100000000) - rva) count = UINT64_C(0x100000000) - rva;
        *offset = (int64_t)raw;
        *available = count < (uint64_t)pe->pInput->size - raw ? count : (uint64_t)pe->pInput->size - raw; return true;
    }
    return false;
}
static uint16_t reader_u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t reader_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static bool reader_token(xx_dotnet_inspection *state, uint32_t token, bool local) {
    unsigned table = token >> 24; uint32_t size; int64_t offset;
    if (local ? table != XX_DOTNET_MDT_StandAloneSig :
        (table != XX_DOTNET_MDT_TypeDef && table != XX_DOTNET_MDT_TypeRef && table != XX_DOTNET_MDT_TypeSpec)) return false;
    return reader_row(state, table, token & 0xffffffU, &offset, &size);
}
bool xx_dotnet_inspect_method_body(xx_dotnet_inspection *state, uint32_t rva,
    xx_dotnet_method_body *body) {
    xx_dotnet_method_body value; uint8_t header[12]; int64_t start; uint64_t available, extent;
    unsigned sections = 0, clauses = 0; uint32_t header_size; bool more;
    if (body) xx_rt_memset(body, 0, sizeof(*body));
    if (!reader_begin(state) || !body || !reader_raw_range(state, rva, &start, &available) ||
        !xx_exec_read(state->pe.pInput, start, header, 1)) return false;
    xx_rt_memset(&value, 0, sizeof(value)); value.header_offset = start;
    if ((header[0] & 3) == 2) {
        header_size = 1; value.flags = 2; value.max_stack = 8; value.code_size = header[0] >> 2; more = false;
    } else if ((header[0] & 3) == 3) {
        uint16_t flags;
        if ((rva & 3) || available < 12 || !xx_exec_read(state->pe.pInput, start, header, 12)) return false;
        flags = reader_u16(header);
        if ((flags >> 12) != 3 || (flags & 0x0fe4)) return false;
        header_size = 12; value.flags = flags & 0xfff; value.max_stack = reader_u16(header + 2);
        value.code_size = reader_u32(header + 4); value.local_signature_token = reader_u32(header + 8);
        value.init_locals = (flags & 0x10) != 0; more = (flags & 8) != 0;
        if (value.local_signature_token && !reader_token(state, value.local_signature_token, true)) return false;
    } else return false;
    extent = (uint64_t)header_size + value.code_size;
    if (value.code_size > DOTNET_READER_MAX_CODE || extent > available) return false;
    value.code_offset = start + header_size;
    while (more) {
        uint8_t section_header[4]; uint32_t section_size, clause_size, count, i; bool fat;
        extent = (extent + 3) & ~UINT64_C(3);
        if (++sections > DOTNET_READER_MAX_SECTIONS || extent > available || available - extent < 4 ||
            !xx_exec_read(state->pe.pInput, start + (int64_t)extent, section_header, 4)) return false;
        if ((section_header[0] & 0x3f) != 1) return false; /* Only EH tables are defined here. */
        fat = (section_header[0] & 0x40) != 0; more = (section_header[0] & 0x80) != 0;
        section_size = fat ? (uint32_t)section_header[1] | ((uint32_t)section_header[2] << 8) | ((uint32_t)section_header[3] << 16) : section_header[1];
        clause_size = fat ? 24 : 12;
        if ((!fat && (section_header[2] || section_header[3])) || section_size < 4 || section_size > available - extent ||
            (section_size - 4) % clause_size) return false;
        count = (section_size - 4) / clause_size;
        if (count > DOTNET_READER_MAX_CLAUSES - clauses) return false;
        clauses += count; value.has_exception_handlers = true;
        for (i = 0; i < count; ++i) {
            uint8_t clause[24]; uint32_t flags, try_offset, try_size, handler_offset, handler_size, extra;
            if (!xx_exec_read(state->pe.pInput, start + (int64_t)extent + 4 + (int64_t)i * clause_size, clause, clause_size)) return false;
            if (fat) {
                flags = reader_u32(clause); try_offset = reader_u32(clause + 4); try_size = reader_u32(clause + 8);
                handler_offset = reader_u32(clause + 12); handler_size = reader_u32(clause + 16); extra = reader_u32(clause + 20);
            } else {
                flags = reader_u16(clause); try_offset = reader_u16(clause + 2); try_size = clause[4];
                handler_offset = reader_u16(clause + 5); handler_size = clause[7]; extra = reader_u32(clause + 8);
            }
            if ((flags != 0 && flags != 1 && flags != 2 && flags != 4) ||
                try_offset > value.code_size || try_size > value.code_size - try_offset ||
                handler_offset > value.code_size || handler_size > value.code_size - handler_offset ||
                (flags == 1 && extra >= value.code_size) || (flags == 0 && !reader_token(state, extra, false))) return false;
        }
        extent += section_size;
    }
    if (extent > UINT32_MAX || state->pe.pInput->failed || xx_pd_is_stopped(state->pe.pInput->pd)) return false;
    value.total_size = (uint32_t)extent; *body = value; return true;
}
