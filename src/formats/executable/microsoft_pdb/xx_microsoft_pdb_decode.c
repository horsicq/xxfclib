/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CodeView symbol and type/id record decoders for Microsoft PDB.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_debug.h"
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb.h"
#include "xxfclib/formats/executable/microsoft_pdb/xx_microsoft_pdb_def.h"
#include "xx_microsoft_pdb_kinds.h"
#include "xx_microsoft_pdb_regs.h"

typedef struct decode_reader {
    const uint8_t *data;
    size_t size;
    size_t offset;
    bool error;
} decode_reader;

static inline void decode_r_init(decode_reader *r, const uint8_t *data, size_t size)
{
    r->data = data;
    r->size = size;
    r->offset = 0;
    r->error = false;
}

static inline uint8_t decode_r_u8(decode_reader *r)
{
    if (r->offset >= r->size) {
        r->error = true;
        return 0;
    }
    return r->data[r->offset++];
}

static inline int8_t decode_r_i8(decode_reader *r)
{
    return (int8_t)decode_r_u8(r);
}

static inline uint16_t decode_r_u16(decode_reader *r)
{
    if (r->offset + 2 > r->size) {
        r->error = true;
        return 0;
    }
    uint16_t v = (uint16_t)(r->data[r->offset] | (r->data[r->offset + 1] << 8));
    r->offset += 2;
    return v;
}

static inline int16_t decode_r_i16(decode_reader *r)
{
    return (int16_t)decode_r_u16(r);
}

static inline uint32_t decode_r_u32(decode_reader *r)
{
    if (r->offset + 4 > r->size) {
        r->error = true;
        return 0;
    }
    uint32_t v =
        (uint32_t)r->data[r->offset] | ((uint32_t)r->data[r->offset + 1] << 8) | ((uint32_t)r->data[r->offset + 2] << 16) | ((uint32_t)r->data[r->offset + 3] << 24);
    r->offset += 4;
    return v;
}

static inline int32_t decode_r_i32(decode_reader *r)
{
    return (int32_t)decode_r_u32(r);
}

static inline uint64_t decode_r_u64(decode_reader *r)
{
    if (r->offset + 8 > r->size) {
        r->error = true;
        return 0;
    }
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= ((uint64_t)r->data[r->offset + i]) << (i * 8);
    }
    r->offset += 8;
    return v;
}

static inline int64_t decode_r_i64(decode_reader *r)
{
    return (int64_t)decode_r_u64(r);
}

static inline float decode_r_f32(decode_reader *r)
{
    uint32_t u = decode_r_u32(r);
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static inline double decode_r_f64(decode_reader *r)
{
    uint64_t u = decode_r_u64(r);
    double d;
    memcpy(&d, &u, sizeof(d));
    return d;
}

static const char *decode_r_cstring(decode_reader *r, char *buf, size_t buf_size)
{
    if (r->offset >= r->size) {
        if (buf_size > 0) buf[0] = '\0';
        return "";
    }
    size_t start = r->offset;
    while (r->offset < r->size && r->data[r->offset] != '\0') {
        r->offset++;
    }
    if (r->offset < r->size && r->data[r->offset] == '\0') {
        const char *s = (const char *)(r->data + start);
        r->offset++;
        return s;
    }
    /* Not NUL terminated within record, copy to buffer */
    size_t len = r->offset - start;
    if (len >= buf_size) len = buf_size - 1;
    if (buf_size > 0) {
        memcpy(buf, r->data + start, len);
        buf[len] = '\0';
        return buf;
    }
    return "";
}

static const char *decode_r_st_string(decode_reader *r, char *buf, size_t buf_size)
{
    uint8_t len = decode_r_u8(r);
    if (r->offset + len > r->size) {
        r->error = true;
        len = (uint8_t)(r->size - r->offset);
    }
    size_t copy_len = len < buf_size - 1 ? len : buf_size - 1;
    if (buf_size > 0) {
        if (copy_len > 0) memcpy(buf, r->data + r->offset, copy_len);
        buf[copy_len] = '\0';
    }
    r->offset += len;
    return buf;
}

/* Event emission helpers */

static inline bool decode_emit_begin(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, int64_t offset, int32_t stream)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_BEGIN;
    ev.value_kind = XX_MS_PDB_FV_NONE;
    ev.depth = depth;
    ev.name = name;
    ev.s = "";
    ev.offset = offset;
    ev.stream = stream;
    return fn(user, &ev);
}

static inline bool decode_emit_end(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_END;
    ev.value_kind = XX_MS_PDB_FV_NONE;
    ev.depth = depth;
    ev.name = name;
    ev.s = "";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_uint(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, uint64_t val)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_UINT;
    ev.depth = depth;
    ev.name = name;
    ev.u = val;
    ev.i = (int64_t)val;
    ev.s = "";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_int(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, int64_t val)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_INT;
    ev.depth = depth;
    ev.name = name;
    ev.u = (uint64_t)val;
    ev.i = val;
    ev.s = "";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_hex(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, uint64_t val)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_HEX;
    ev.depth = depth;
    ev.name = name;
    ev.u = val;
    ev.i = (int64_t)val;
    ev.s = "";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_bool(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, bool val)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_BOOL;
    ev.depth = depth;
    ev.name = name;
    ev.u = val ? 1 : 0;
    ev.i = val ? 1 : 0;
    ev.s = val ? "true" : "false";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_string(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, const char *str)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_STRING;
    ev.depth = depth;
    ev.name = name;
    ev.s = str ? str : "";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_type(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, uint32_t type_index)
{
    char buf[32];
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_TYPE;
    ev.depth = depth;
    ev.name = name;
    ev.u = type_index;
    ev.i = (int64_t)type_index;
    if (type_index < 0x1000) {
        ev.s = xx_ms_pdb_primitive_type_to_string(type_index);
    } else {
        snprintf(buf, sizeof(buf), "0x%X", (unsigned)type_index);
        ev.s = buf;
    }
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_id(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, uint32_t id_index)
{
    char buf[32];
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_ID;
    ev.depth = depth;
    ev.name = name;
    ev.u = id_index;
    ev.i = (int64_t)id_index;
    if (id_index < 0x1000) {
        ev.s = xx_ms_pdb_primitive_type_to_string(id_index);
    } else {
        snprintf(buf, sizeof(buf), "0x%X", (unsigned)id_index);
        ev.s = buf;
    }
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_enum(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, uint64_t val, const char *enum_name)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_ENUM;
    ev.depth = depth;
    ev.name = name;
    ev.u = val;
    ev.i = (int64_t)val;
    ev.s = enum_name ? enum_name : "";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_flags(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, uint64_t val, const char *flags_str)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_FLAGS;
    ev.depth = depth;
    ev.name = name;
    ev.u = val;
    ev.i = (int64_t)val;
    ev.s = flags_str ? flags_str : "";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_bytes(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, const uint8_t *bytes, size_t size)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_BYTES;
    ev.depth = depth;
    ev.name = name;
    ev.bytes = bytes;
    ev.size = size;
    ev.s = "";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_guid(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, const uint8_t *g)
{
    char buf[40];
    if (g) {
        snprintf(buf, sizeof(buf), "{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9],
                 g[10], g[11], g[12], g[13], g[14], g[15]);
    } else {
        snprintf(buf, sizeof(buf), "{00000000-0000-0000-0000-000000000000}");
    }
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_GUID;
    ev.depth = depth;
    ev.name = name;
    ev.s = buf;
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline bool decode_emit_float(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, double val)
{
    xx_ms_pdb_dump_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.event = XX_MS_PDB_EV_FIELD;
    ev.value_kind = XX_MS_PDB_FV_FLOAT;
    ev.depth = depth;
    ev.name = name;
    ev.d = val;
    ev.s = "";
    ev.offset = -1;
    ev.stream = -1;
    return fn(user, &ev);
}

static inline void decode_check_cut(decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    if (r->error) {
        decode_emit_string(fn, user, depth, "error", "truncated record");
        return;
    }
    /* Skip alignment padding bytes at the end of a record (0x00 or LF_PAD0..LF_PAD3 0xF0..0xFF) */
    while (r->offset < r->size) {
        uint8_t b = r->data[r->offset];
        if (b == 0 || (b >= 0xF0 && b <= 0xFF)) {
            r->offset++;
        } else {
            break;
        }
    }
    /* Any trailing bytes <= 3 bytes at the end of a record are alignment padding */
    if (r->size - r->offset < 4) {
        r->offset = r->size;
        return;
    }
    if (r->offset < r->size) {
        decode_emit_string(fn, user, depth, "error", "trailing bytes");
        decode_emit_bytes(fn, user, depth, "data", r->data + r->offset, r->size - r->offset);
        r->offset = r->size;
    }
}

/* Enumeration string conversions */

static const char *decode_language_to_string(uint32_t lang)
{
    switch (lang) {
        case 0: return "C";
        case 1: return "C++";
        case 2: return "Fortran";
        case 3: return "MASM";
        case 4: return "Pascal";
        case 5: return "Basic";
        case 6: return "COBOL";
        case 7: return "LINK";
        case 8: return "CVTRES";
        case 9: return "CVTPGD";
        case 10: return "C#";
        case 11: return "Visual Basic";
        case 12: return "ILASM";
        case 13: return "Java";
        case 14: return "JScript";
        case 15: return "MSIL";
        case 16: return "HLSL";
        case 17: return "Objective-C";
        case 18: return "Objective-C++";
        case 19: return "Swift";
        case 20: return "AliasObj";
        case 21: return "Rust";
        case 22: return "Go";
        case 0x44: return "D";
        default: return "Unknown";
    }
}

static const char *decode_cpu_to_string(uint16_t cpu)
{
    switch (cpu) {
        case 0x00: return "Intel 8080";
        case 0x01: return "Intel 8086";
        case 0x02: return "Intel 80286";
        case 0x03: return "Intel 80386";
        case 0x04: return "Intel 80486";
        case 0x05: return "Intel Pentium";
        case 0x06: return "Intel Pentium Pro / II";
        case 0x07: return "Intel Pentium III";
        case 0x10: return "MIPS";
        case 0x11: return "MIPS16";
        case 0x12: return "MIPS32";
        case 0x13: return "MIPS64";
        case 0x20: return "M68000";
        case 0x30: return "Alpha";
        case 0x40: return "PPC";
        case 0x50: return "SH3";
        case 0x53: return "SH4";
        case 0x60: return "ARM";
        case 0x68: return "ARM7";
        case 0x80: return "IA64";
        case 0xD0: return "Intel x86-x64";
        case 0xE0: return "EBC";
        case 0xF0: return "THUMB";
        case 0xF4: return "ARMNT";
        case 0xF6: return "ARM64";
        default: return "Unknown";
    }
}

static const char *decode_calling_convention(uint8_t cc)
{
    switch (cc) {
        case 0x00: return "cdecl";
        case 0x01: return "pascal";
        case 0x02: return "thiscall";
        case 0x03: return "stdcall";
        case 0x04: return "fastcall";
        case 0x07: return "stdcall";
        case 0x0B: return "thiscall";
        case 0x17: return "inline";
        case 0x18: return "vectorcall";
        default: return "cdecl";
    }
}

static const char *decode_thunk_ordinal(uint8_t ord)
{
    switch (ord) {
        case 0: return "standard";
        case 1: return "this adjustor";
        case 2: return "vcall";
        case 3: return "pcode";
        case 4: return "load";
        case 5: return "trampoline incremental";
        case 6: return "trampoline branch island";
        default: return "unknown";
    }
}

/* Numeric leaf decoder */
typedef struct decode_num_val {
    uint16_t leaf;
    int64_t ival;
    uint64_t uval;
    double dval;
    bool is_signed;
    bool is_float;
    bool is_str;
    char sval[64];
} decode_num_val;

static void decode_read_numeric(decode_reader *r, decode_num_val *nv)
{
    memset(nv, 0, sizeof(*nv));
    nv->leaf = decode_r_u16(r);
    if (nv->leaf < XX_MS_PDB_LF_NUMERIC) {
        nv->uval = nv->leaf;
        nv->ival = nv->leaf;
        return;
    }
    switch (nv->leaf) {
        case XX_MS_PDB_LF_CHAR:
            nv->ival = decode_r_i8(r);
            nv->uval = (uint64_t)nv->ival;
            nv->is_signed = true;
            break;
        case XX_MS_PDB_LF_SHORT:
            nv->ival = decode_r_i16(r);
            nv->uval = (uint64_t)nv->ival;
            nv->is_signed = true;
            break;
        case XX_MS_PDB_LF_USHORT:
            nv->uval = decode_r_u16(r);
            nv->ival = (int64_t)nv->uval;
            break;
        case XX_MS_PDB_LF_LONG:
            nv->ival = decode_r_i32(r);
            nv->uval = (uint64_t)nv->ival;
            nv->is_signed = true;
            break;
        case XX_MS_PDB_LF_ULONG:
            nv->uval = decode_r_u32(r);
            nv->ival = (int64_t)nv->uval;
            break;
        case XX_MS_PDB_LF_REAL32:
            nv->dval = decode_r_f32(r);
            nv->is_float = true;
            break;
        case XX_MS_PDB_LF_REAL64:
            nv->dval = decode_r_f64(r);
            nv->is_float = true;
            break;
        case XX_MS_PDB_LF_REAL80:
            /* 10 bytes float */
            if (r->offset + 10 <= r->size) {
                nv->dval = decode_r_f64(r);
                r->offset += 2; /* remaining 2 bytes */
            } else {
                r->error = true;
            }
            nv->is_float = true;
            break;
        case XX_MS_PDB_LF_REAL128:
            /* 16 bytes float */
            if (r->offset + 16 <= r->size) {
                nv->dval = decode_r_f64(r);
                r->offset += 8;
            } else {
                r->error = true;
            }
            nv->is_float = true;
            break;
        case XX_MS_PDB_LF_QUADWORD:
            nv->ival = decode_r_i64(r);
            nv->uval = (uint64_t)nv->ival;
            nv->is_signed = true;
            break;
        case XX_MS_PDB_LF_UQUADWORD:
            nv->uval = decode_r_u64(r);
            nv->ival = (int64_t)nv->uval;
            break;
        case XX_MS_PDB_LF_REAL48:
            if (r->offset + 6 <= r->size) r->offset += 6;
            else r->error = true;
            nv->dval = 0;
            nv->is_float = true;
            break;
        case XX_MS_PDB_LF_COMPLEX32:
            if (r->offset + 8 <= r->size) r->offset += 8;
            else r->error = true;
            nv->is_str = true;
            snprintf(nv->sval, sizeof(nv->sval), "<complex32>");
            break;
        case XX_MS_PDB_LF_COMPLEX64:
            if (r->offset + 16 <= r->size) r->offset += 16;
            else r->error = true;
            nv->is_str = true;
            snprintf(nv->sval, sizeof(nv->sval), "<complex64>");
            break;
        case XX_MS_PDB_LF_COMPLEX80:
            if (r->offset + 20 <= r->size) r->offset += 20;
            else r->error = true;
            nv->is_str = true;
            snprintf(nv->sval, sizeof(nv->sval), "<complex80>");
            break;
        case XX_MS_PDB_LF_COMPLEX128:
            if (r->offset + 32 <= r->size) r->offset += 32;
            else r->error = true;
            nv->is_str = true;
            snprintf(nv->sval, sizeof(nv->sval), "<complex128>");
            break;
        case XX_MS_PDB_LF_VARSTRING: {
            uint16_t vlen = decode_r_u16(r);
            if (r->offset + vlen <= r->size) {
                size_t cplen = vlen < sizeof(nv->sval) - 1 ? vlen : sizeof(nv->sval) - 1;
                memcpy(nv->sval, r->data + r->offset, cplen);
                nv->sval[cplen] = '\0';
                r->offset += vlen;
            } else {
                r->error = true;
            }
            nv->is_str = true;
            break;
        }
        case XX_MS_PDB_LF_OCTWORD:
        case XX_MS_PDB_LF_UOCTWORD: {
            uint64_t low = decode_r_u64(r);
            uint64_t high = decode_r_u64(r);
            nv->is_str = true;
            snprintf(nv->sval, sizeof(nv->sval), "0x%llX%016llX", (unsigned long long)high, (unsigned long long)low);
            break;
        }
        case XX_MS_PDB_LF_DECIMAL:
            if (r->offset + 16 <= r->size) r->offset += 16;
            else r->error = true;
            nv->is_str = true;
            snprintf(nv->sval, sizeof(nv->sval), "<decimal>");
            break;
        case XX_MS_PDB_LF_DATE:
            if (r->offset + 8 <= r->size) r->offset += 8;
            else r->error = true;
            nv->is_str = true;
            snprintf(nv->sval, sizeof(nv->sval), "<date>");
            break;
        case XX_MS_PDB_LF_UTF8STRING:
            decode_r_cstring(r, nv->sval, sizeof(nv->sval));
            nv->is_str = true;
            break;
        case XX_MS_PDB_LF_REAL16:
            decode_r_u16(r);
            nv->is_float = true;
            break;
        default: r->error = true; break;
    }
}

static void decode_emit_num_val(xx_ms_pdb_dump_fn fn, void *user, int32_t depth, const char *name, const decode_num_val *nv)
{
    if (nv->is_str) {
        decode_emit_string(fn, user, depth, name, nv->sval);
    } else if (nv->is_float) {
        decode_emit_float(fn, user, depth, name, nv->dval);
    } else if (nv->is_signed) {
        decode_emit_int(fn, user, depth, name, nv->ival);
    } else {
        decode_emit_uint(fn, user, depth, name, nv->uval);
    }
}

/* Binary annotations decoder for S_INLINESITE / S_INLINESITE2 */
static uint32_t decode_read_compressed_u32(decode_reader *r)
{
    uint8_t b0 = decode_r_u8(r);
    if ((b0 & 0x80) == 0) {
        return b0;
    } else if ((b0 & 0xC0) == 0x80) {
        uint8_t b1 = decode_r_u8(r);
        return ((uint32_t)(b0 & 0x3F) << 8) | b1;
    } else if ((b0 & 0xE0) == 0xC0) {
        uint8_t b1 = decode_r_u8(r);
        uint8_t b2 = decode_r_u8(r);
        uint8_t b3 = decode_r_u8(r);
        return ((uint32_t)(b0 & 0x1F) << 24) | ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | b3;
    }
    return 0;
}

static int32_t decode_decode_signed(uint32_t val)
{
    if (val & 1) {
        return -(int32_t)(val >> 1);
    } else {
        return (int32_t)(val >> 1);
    }
}

static const char *decode_binary_annotation_op_to_string(uint32_t op)
{
    switch (op) {
        case 0: return "Invalid";
        case 1: return "CodeOffset";
        case 2: return "ChangeCodeOffsetBase";
        case 3: return "ChangeCodeOffset";
        case 4: return "ChangeCodeLength";
        case 5: return "ChangeFile";
        case 6: return "ChangeLineOffset";
        case 7: return "ChangeLineEndDelta";
        case 8: return "ChangeRangeKind";
        case 9: return "ChangeColumnStart";
        case 10: return "ChangeColumnEndDelta";
        case 11: return "ChangeCodeOffsetAndLineOffset";
        case 12: return "ChangeCodeLengthAndCodeOffset";
        case 13: return "ChangeColumnEnd";
        default: return NULL;
    }
}

static void decode_binary_annotations(decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    while (r->offset < r->size && !r->error) {
        uint32_t op = decode_read_compressed_u32(r);
        if (op == 0) break; /* Eof / Invalid */
        const char *op_name = decode_binary_annotation_op_to_string(op);
        decode_emit_begin(fn, user, depth, "annotation", -1, -1);
        decode_emit_enum(fn, user, depth + 1, "op", op, op_name);
        switch (op) {
            case 1: { /* CodeOffset */
                uint32_t off = decode_read_compressed_u32(r);
                decode_emit_hex(fn, user, depth + 1, "code_offset", off);
                break;
            }
            case 2: { /* ChangeCodeOffsetBase */
                uint32_t base = decode_read_compressed_u32(r);
                decode_emit_hex(fn, user, depth + 1, "code_offset_base", base);
                break;
            }
            case 3: { /* ChangeCodeOffset */
                uint32_t delta = decode_read_compressed_u32(r);
                decode_emit_hex(fn, user, depth + 1, "code_offset_delta", delta);
                break;
            }
            case 4: { /* ChangeCodeLength */
                uint32_t len = decode_read_compressed_u32(r);
                decode_emit_uint(fn, user, depth + 1, "code_length", len);
                break;
            }
            case 5: { /* ChangeFile */
                uint32_t file_id = decode_read_compressed_u32(r);
                decode_emit_hex(fn, user, depth + 1, "file_id", file_id);
                break;
            }
            case 6: { /* ChangeLineOffset */
                uint32_t raw = decode_read_compressed_u32(r);
                decode_emit_int(fn, user, depth + 1, "line_offset", decode_decode_signed(raw));
                break;
            }
            case 7: { /* ChangeLineEndDelta */
                uint32_t delta = decode_read_compressed_u32(r);
                decode_emit_uint(fn, user, depth + 1, "line_end_delta", delta);
                break;
            }
            case 8: { /* ChangeRangeKind */
                uint32_t kind = decode_read_compressed_u32(r);
                decode_emit_uint(fn, user, depth + 1, "range_kind", kind);
                break;
            }
            case 9: { /* ChangeColumnStart */
                uint32_t col = decode_read_compressed_u32(r);
                decode_emit_uint(fn, user, depth + 1, "column_start", col);
                break;
            }
            case 10: { /* ChangeColumnEndDelta */
                uint32_t raw = decode_read_compressed_u32(r);
                decode_emit_int(fn, user, depth + 1, "column_end_delta", decode_decode_signed(raw));
                break;
            }
            case 11: { /* ChangeCodeOffsetAndLineOffset */
                uint32_t val = decode_read_compressed_u32(r);
                uint32_t code_delta = val & 0x0F;
                int32_t line_delta = decode_decode_signed(val >> 4);
                decode_emit_hex(fn, user, depth + 1, "code_offset_delta", code_delta);
                decode_emit_int(fn, user, depth + 1, "line_offset", line_delta);
                break;
            }
            case 12: { /* ChangeCodeLengthAndCodeOffset */
                uint32_t len = decode_read_compressed_u32(r);
                uint32_t code_delta = decode_read_compressed_u32(r);
                decode_emit_uint(fn, user, depth + 1, "code_length", len);
                decode_emit_hex(fn, user, depth + 1, "code_offset_delta", code_delta);
                break;
            }
            case 13: { /* ChangeColumnEnd */
                uint32_t col = decode_read_compressed_u32(r);
                decode_emit_uint(fn, user, depth + 1, "column_end", col);
                break;
            }
            default: {
                uint32_t arg = decode_read_compressed_u32(r);
                decode_emit_uint(fn, user, depth + 1, "operand", arg);
                break;
            }
        }
        decode_emit_end(fn, user, depth, "annotation");
    }
}

static void decode_format_prop_flags(uint32_t prop, char *buf, size_t buf_size)
{
    buf[0] = '\0';
    size_t len = 0;
#define PROP_FLAG(bit, name)                 \
    if (prop & (bit)) {                      \
        if (len > 0 && len + 3 < buf_size) { \
            strcat(buf, " | ");              \
            len += 3;                        \
        }                                    \
        if (len + strlen(name) < buf_size) { \
            strcat(buf, name);               \
            len += strlen(name);             \
        }                                    \
    }
    PROP_FLAG(0x0001, "packed")
    PROP_FLAG(0x0002, "ctor")
    PROP_FLAG(0x0004, "ovlops")
    PROP_FLAG(0x0008, "is_nested")
    PROP_FLAG(0x0010, "cnested")
    PROP_FLAG(0x0020, "opassign")
    PROP_FLAG(0x0040, "opcast")
    PROP_FLAG(0x0080, "forward_ref")
    PROP_FLAG(0x0100, "scoped")
    PROP_FLAG(0x0200, "has_unique_name")
    PROP_FLAG(0x0400, "sealed")
    PROP_FLAG(0x0800, "hfa_float")
    PROP_FLAG(0x1000, "hfa_double")
    PROP_FLAG(0x2000, "intrinsic")
#undef PROP_FLAG
    if (len == 0 && buf_size > 4) strcpy(buf, "none");
}

static void decode_format_fldattr(uint16_t attr, char *buf, size_t buf_size)
{
    buf[0] = '\0';
    uint8_t access = (uint8_t)(attr & 0x03);
    uint8_t mprop = (uint8_t)((attr >> 2) & 0x07);
    const char *acc_str = (access == 1) ? "private" : ((access == 2) ? "protected" : ((access == 3) ? "public" : "no_access"));
    const char *mp_str = "";
    switch (mprop) {
        case 0: mp_str = "vanilla"; break;
        case 1: mp_str = "virtual"; break;
        case 2: mp_str = "static"; break;
        case 3: mp_str = "friend"; break;
        case 4: mp_str = "intro"; break;
        case 5: mp_str = "pure_virtual"; break;
        case 6: mp_str = "pure_intro"; break;
        default: mp_str = "unknown"; break;
    }
    snprintf(buf, buf_size, "%s | %s", acc_str, mp_str);
    if (attr & 0x20) {
        if (strlen(buf) + 12 < buf_size) strcat(buf, " | compgen");
    }
    if (attr & 0x40) {
        if (strlen(buf) + 12 < buf_size) strcat(buf, " | sealed");
    }
}

static void decode_format_frameproc_flags(uint32_t flags, char *buf, size_t buf_size)
{
    buf[0] = '\0';
    size_t len = 0;
#define FP_FLAG(bit, name)                   \
    if (flags & (bit)) {                     \
        if (len > 0 && len + 3 < buf_size) { \
            strcat(buf, " | ");              \
            len += 3;                        \
        }                                    \
        if (len + strlen(name) < buf_size) { \
            strcat(buf, name);               \
            len += strlen(name);             \
        }                                    \
    }
    FP_FLAG(0x00000001, "has_alloca")
    FP_FLAG(0x00000002, "has_setjmp")
    FP_FLAG(0x00000004, "has_longjmp")
    FP_FLAG(0x00000008, "has_inl_asm")
    FP_FLAG(0x00000010, "has_eh")
    FP_FLAG(0x00000020, "has_inl_spec")
    FP_FLAG(0x00000040, "naked")
    FP_FLAG(0x00000080, "security_checks")
    FP_FLAG(0x00000100, "async_eh")
    FP_FLAG(0x00000200, "no_stack_ordering")
    FP_FLAG(0x00000400, "was_inlined")
    FP_FLAG(0x00000800, "gs_check")
    FP_FLAG(0x00001000, "safe_buffers")
    FP_FLAG(0x00040000, "strict_gs_check")
#undef FP_FLAG
    if (len == 0 && buf_size > 4) strcpy(buf, "none");
}

static const char *decode_frameproc_base_reg(uint16_t machine, uint8_t enc)
{
    if (enc == 0) return "none";
    if (machine == 0xD0 || machine == 0x8664) { /* x64 */
        if (enc == 1) return "RSP";
        if (enc == 2) return "RBP";
        if (enc == 3) return "R13";
    } else if (machine == 0xF6 || machine == 0xF8 || machine == 0xF9 || machine == 0xAA64) { /* ARM64 */
        if (enc == 1) return "SP";
        if (enc == 2) return "FP";
        if (enc == 3) return "X19";
    } else if ((machine >= 0x60 && machine <= 0x68) || machine == 0xF0 || machine == 0xF4 || machine == 0x01C0 || machine == 0x01C2 || machine == 0x01C4) { /* ARM */
        if (enc == 1) return "SP";
        if (enc == 2) return "R11";
        if (enc == 3) return "R7";
    } else { /* x86 default */
        if (enc == 1) return "VFRAME";
        if (enc == 2) return "EBP";
        if (enc == 3) return "EBX";
    }
    return "unknown";
}

static void decode_symbol_compile(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    if (kind == XX_MS_PDB_S_COMPILE3) {
        uint32_t flags = decode_r_u32(r);
        uint8_t lang = (uint8_t)(flags & 0xFF);
        uint16_t machine = decode_r_u16(r);
        uint16_t fe_major = decode_r_u16(r);
        uint16_t fe_minor = decode_r_u16(r);
        uint16_t fe_build = decode_r_u16(r);
        uint16_t fe_qfe = decode_r_u16(r);
        uint16_t be_major = decode_r_u16(r);
        uint16_t be_minor = decode_r_u16(r);
        uint16_t be_build = decode_r_u16(r);
        uint16_t be_qfe = decode_r_u16(r);
        const char *ver = decode_r_cstring(r, str_buf, sizeof(str_buf));
        decode_emit_enum(fn, user, depth, "machine", machine, decode_cpu_to_string(machine));
        decode_emit_enum(fn, user, depth, "language", lang, decode_language_to_string(lang));
        decode_emit_hex(fn, user, depth, "flags", flags >> 8);
        decode_emit_uint(fn, user, depth, "frontend_major", fe_major);
        decode_emit_uint(fn, user, depth, "frontend_minor", fe_minor);
        decode_emit_uint(fn, user, depth, "frontend_build", fe_build);
        decode_emit_uint(fn, user, depth, "frontend_qfe", fe_qfe);
        decode_emit_uint(fn, user, depth, "backend_major", be_major);
        decode_emit_uint(fn, user, depth, "backend_minor", be_minor);
        decode_emit_uint(fn, user, depth, "backend_build", be_build);
        decode_emit_uint(fn, user, depth, "backend_qfe", be_qfe);
        decode_emit_string(fn, user, depth, "version", ver);
    } else if (kind == XX_MS_PDB_S_COMPILE2 || kind == XX_MS_PDB_S_COMPILE2_ST) {
        uint32_t flags = decode_r_u32(r);
        uint8_t lang = (uint8_t)(flags & 0xFF);
        uint16_t machine = decode_r_u16(r);
        uint16_t fe_major = decode_r_u16(r);
        uint16_t fe_minor = decode_r_u16(r);
        uint16_t fe_build = decode_r_u16(r);
        uint16_t be_major = decode_r_u16(r);
        uint16_t be_minor = decode_r_u16(r);
        uint16_t be_build = decode_r_u16(r);
        const char *ver = (kind == XX_MS_PDB_S_COMPILE2_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
        decode_emit_enum(fn, user, depth, "machine", machine, decode_cpu_to_string(machine));
        decode_emit_enum(fn, user, depth, "language", lang, decode_language_to_string(lang));
        decode_emit_hex(fn, user, depth, "flags", flags >> 8);
        decode_emit_uint(fn, user, depth, "frontend_major", fe_major);
        decode_emit_uint(fn, user, depth, "frontend_minor", fe_minor);
        decode_emit_uint(fn, user, depth, "frontend_build", fe_build);
        decode_emit_uint(fn, user, depth, "backend_major", be_major);
        decode_emit_uint(fn, user, depth, "backend_minor", be_minor);
        decode_emit_uint(fn, user, depth, "backend_build", be_build);
        decode_emit_string(fn, user, depth, "version", ver);
        /* Extra compile strings */
        while (r->offset < r->size && !r->error) {
            const char *extra = decode_r_cstring(r, str_buf, sizeof(str_buf));
            if (extra && extra[0]) decode_emit_string(fn, user, depth, "extra_string", extra);
        }
    } else { /* S_COMPILE */
        uint8_t machine = decode_r_u8(r);
        uint8_t lang = decode_r_u8(r);
        uint16_t flags = decode_r_u16(r);
        const char *ver = decode_r_cstring(r, str_buf, sizeof(str_buf));
        decode_emit_enum(fn, user, depth, "machine", machine, decode_cpu_to_string(machine));
        decode_emit_enum(fn, user, depth, "language", lang, decode_language_to_string(lang));
        decode_emit_hex(fn, user, depth, "flags", flags);
        decode_emit_string(fn, user, depth, "version", ver);
    }
}

static void decode_symbol_proc(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    uint32_t parent = decode_r_u32(r);
    uint32_t end = decode_r_u32(r);
    uint32_t next = decode_r_u32(r);
    uint32_t code_size = decode_r_u32(r);
    uint32_t dbg_start = decode_r_u32(r);
    uint32_t dbg_end = decode_r_u32(r);
    uint32_t type_index = 0;
    bool is_id = (kind == XX_MS_PDB_S_LPROC32_ID || kind == XX_MS_PDB_S_GPROC32_ID || kind == XX_MS_PDB_S_LPROC32_DPC_ID || kind == XX_MS_PDB_S_LPROCMIPS_ID ||
                  kind == XX_MS_PDB_S_GPROCMIPS_ID || kind == XX_MS_PDB_S_LPROCIA64_ID || kind == XX_MS_PDB_S_GPROCIA64_ID);
    bool is_16t = (kind == XX_MS_PDB_S_LPROC32_16t || kind == XX_MS_PDB_S_GPROC32_16t || kind == XX_MS_PDB_S_LPROCMIPS_16t || kind == XX_MS_PDB_S_GPROCMIPS_16t ||
                   kind == XX_MS_PDB_S_LPROC16 || kind == XX_MS_PDB_S_GPROC16);
    bool is_st = (kind == XX_MS_PDB_S_LPROC32_ST || kind == XX_MS_PDB_S_GPROC32_ST || kind == XX_MS_PDB_S_LPROCMIPS_ST || kind == XX_MS_PDB_S_GPROCMIPS_ST ||
                  kind == XX_MS_PDB_S_LPROCIA64_ST || kind == XX_MS_PDB_S_GPROCIA64_ST);
    if (is_16t) {
        type_index = decode_r_u16(r);
    } else {
        type_index = decode_r_u32(r);
    }
    uint32_t offset = decode_r_u32(r);
    uint16_t segment = decode_r_u16(r);
    uint8_t flags = decode_r_u8(r);
    const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));

    decode_emit_hex(fn, user, depth, "parent", parent);
    decode_emit_hex(fn, user, depth, "end", end);
    decode_emit_hex(fn, user, depth, "next", next);
    decode_emit_uint(fn, user, depth, "code_size", code_size);
    decode_emit_hex(fn, user, depth, "dbg_start", dbg_start);
    decode_emit_hex(fn, user, depth, "dbg_end", dbg_end);
    if (is_id) {
        decode_emit_id(fn, user, depth, "id_index", type_index);
    } else {
        decode_emit_type(fn, user, depth, "type_index", type_index);
    }
    decode_emit_hex(fn, user, depth, "offset", offset);
    decode_emit_uint(fn, user, depth, "segment", segment);
    decode_emit_hex(fn, user, depth, "flags", flags);
    decode_emit_string(fn, user, depth, "name", name);
}

static void decode_symbol_frameproc(decode_reader *r, uint16_t machine, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char flags_buf[256];
    uint32_t frame_size = decode_r_u32(r);
    uint32_t padding_size = decode_r_u32(r);
    uint32_t padding_offset = decode_r_u32(r);
    uint32_t callee_saved_size = decode_r_u32(r);
    uint32_t eh_offset = decode_r_u32(r);
    uint16_t eh_segment = decode_r_u16(r);
    uint32_t flags = decode_r_u32(r);
    uint8_t local_enc = (uint8_t)((flags >> 14) & 3);
    uint8_t param_enc = (uint8_t)((flags >> 16) & 3);
    const char *local_reg = decode_frameproc_base_reg(machine, local_enc);
    const char *param_reg = decode_frameproc_base_reg(machine, param_enc);
    decode_format_frameproc_flags(flags, flags_buf, sizeof(flags_buf));

    decode_emit_uint(fn, user, depth, "frame_size", frame_size);
    decode_emit_uint(fn, user, depth, "padding_size", padding_size);
    decode_emit_hex(fn, user, depth, "padding_offset", padding_offset);
    decode_emit_uint(fn, user, depth, "callee_saved_regs_size", callee_saved_size);
    decode_emit_hex(fn, user, depth, "exception_handler_offset", eh_offset);
    decode_emit_uint(fn, user, depth, "exception_handler_segment", eh_segment);
    decode_emit_flags(fn, user, depth, "flags", flags, flags_buf);
    decode_emit_enum(fn, user, depth, "local_base_reg", local_enc, local_reg);
    decode_emit_enum(fn, user, depth, "param_base_reg", param_enc, param_reg);
}

static void decode_symbol_defrange(uint16_t kind, decode_reader *r, uint16_t machine, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    if (kind == XX_MS_PDB_S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE) {
        int32_t offset = decode_r_i32(r);
        decode_emit_int(fn, user, depth, "offset", offset);
        return;
    }

    if (kind == XX_MS_PDB_S_DEFRANGE_FRAMEPOINTER_REL) {
        int32_t offset = decode_r_i32(r);
        decode_emit_int(fn, user, depth, "offset", offset);
    } else if (kind == XX_MS_PDB_S_DEFRANGE_REGISTER) {
        uint16_t reg = decode_r_u16(r);
        uint16_t attr = decode_r_u16(r);
        decode_emit_enum(fn, user, depth, "register", reg, decode_get_register_name(machine, reg));
        decode_emit_bool(fn, user, depth, "may_have_no_name", (attr & 1) != 0);
    } else if (kind == XX_MS_PDB_S_DEFRANGE_REGISTER_REL) {
        uint16_t reg = decode_r_u16(r);
        uint16_t flags = decode_r_u16(r);
        int32_t offset = decode_r_i32(r);
        decode_emit_enum(fn, user, depth, "register", reg, decode_get_register_name(machine, reg));
        decode_emit_int(fn, user, depth, "offset", offset);
        decode_emit_int(fn, user, depth, "offset_in_parent", (flags >> 4));
        decode_emit_bool(fn, user, depth, "has_spilled_udt", (flags & 1) != 0);
    } else if (kind == XX_MS_PDB_S_DEFRANGE_SUBFIELD_REGISTER) {
        uint16_t reg = decode_r_u16(r);
        uint16_t attr = decode_r_u16(r);
        uint32_t off_parent = decode_r_u32(r);
        decode_emit_enum(fn, user, depth, "register", reg, decode_get_register_name(machine, reg));
        decode_emit_uint(fn, user, depth, "offset_in_parent", off_parent);
        decode_emit_bool(fn, user, depth, "may_have_no_name", (attr & 1) != 0);
    } else if (kind == XX_MS_PDB_S_DEFRANGE_HLSL) {
        uint16_t rlow = decode_r_u16(r);
        uint16_t rhigh = decode_r_u16(r);
        decode_emit_uint(fn, user, depth, "reg_low", rlow);
        decode_emit_uint(fn, user, depth, "reg_high", rhigh);
    } else if (kind == XX_MS_PDB_S_DEFRANGE_DPC_PTR_TAG) {
        uint32_t type_index = decode_r_u32(r);
        decode_emit_type(fn, user, depth, "type_index", type_index);
    }

    /* Range: offStart(u32), isectStart(u16), cbRange(u16) */
    uint32_t off_start = decode_r_u32(r);
    uint16_t isect_start = decode_r_u16(r);
    uint16_t cb_range = decode_r_u16(r);
    decode_emit_begin(fn, user, depth, "range", -1, -1);
    decode_emit_hex(fn, user, depth + 1, "offset", off_start);
    decode_emit_uint(fn, user, depth + 1, "segment", isect_start);
    decode_emit_uint(fn, user, depth + 1, "length", cb_range);
    decode_emit_end(fn, user, depth, "range");

    /* Gaps */
    while (r->offset + 4 <= r->size && !r->error) {
        uint16_t gap_start = decode_r_u16(r);
        uint16_t gap_len = decode_r_u16(r);
        decode_emit_begin(fn, user, depth, "gap", -1, -1);
        decode_emit_hex(fn, user, depth + 1, "offset", gap_start);
        decode_emit_uint(fn, user, depth + 1, "length", gap_len);
        decode_emit_end(fn, user, depth, "gap");
    }
}

static void decode_symbol_constant(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    uint32_t type_index = (kind == XX_MS_PDB_S_CONSTANT_16t) ? decode_r_u16(r) : decode_r_u32(r);
    decode_num_val nv;
    decode_read_numeric(r, &nv);
    const char *name = (kind == XX_MS_PDB_S_CONSTANT_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));

    decode_emit_type(fn, user, depth, "type_index", type_index);
    decode_emit_num_val(fn, user, depth, "value", &nv);
    decode_emit_string(fn, user, depth, "name", name);
}

static void decode_symbol_data(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    bool is_16t = (kind == XX_MS_PDB_S_LDATA32_16t || kind == XX_MS_PDB_S_GDATA32_16t || kind == XX_MS_PDB_S_LTHREAD32_16t || kind == XX_MS_PDB_S_GTHREAD32_16t ||
                   kind == XX_MS_PDB_S_LDATA16 || kind == XX_MS_PDB_S_GDATA16);
    bool is_st = (kind == XX_MS_PDB_S_LDATA32_ST || kind == XX_MS_PDB_S_GDATA32_ST || kind == XX_MS_PDB_S_LTHREAD32_ST || kind == XX_MS_PDB_S_GTHREAD32_ST);
    uint32_t type_index = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    uint32_t offset = (kind == XX_MS_PDB_S_LDATA16 || kind == XX_MS_PDB_S_GDATA16) ? decode_r_u16(r) : decode_r_u32(r);
    uint16_t segment = decode_r_u16(r);
    const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));

    decode_emit_type(fn, user, depth, "type_index", type_index);
    decode_emit_hex(fn, user, depth, "offset", offset);
    decode_emit_uint(fn, user, depth, "segment", segment);
    decode_emit_string(fn, user, depth, "name", name);
}

static void decode_symbol_pub(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    bool is_st = (kind == XX_MS_PDB_S_PUB32_ST);
    uint32_t flags = (kind == XX_MS_PDB_S_PUB16) ? decode_r_u16(r) : decode_r_u32(r);
    uint32_t offset = (kind == XX_MS_PDB_S_PUB16) ? decode_r_u16(r) : decode_r_u32(r);
    uint16_t segment = decode_r_u16(r);
    const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));

    decode_emit_hex(fn, user, depth, "flags", flags);
    decode_emit_hex(fn, user, depth, "offset", offset);
    decode_emit_uint(fn, user, depth, "segment", segment);
    decode_emit_string(fn, user, depth, "name", name);
}

static void decode_symbol_ref(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    bool is_st = (kind == XX_MS_PDB_S_PROCREF_ST || kind == XX_MS_PDB_S_LPROCREF_ST || kind == XX_MS_PDB_S_DATAREF_ST);
    uint32_t checksum = decode_r_u32(r);
    uint32_t offset = decode_r_u32(r);
    uint16_t module = decode_r_u16(r);
    const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));

    decode_emit_hex(fn, user, depth, "checksum", checksum);
    decode_emit_hex(fn, user, depth, "offset", offset);
    decode_emit_uint(fn, user, depth, "module", module);
    decode_emit_string(fn, user, depth, "name", name);
}

static void decode_symbol_thunk(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    bool is_st = (kind == XX_MS_PDB_S_THUNK32_ST);
    uint32_t parent = decode_r_u32(r);
    uint32_t end = decode_r_u32(r);
    uint32_t next = decode_r_u32(r);
    uint32_t offset = decode_r_u32(r);
    uint16_t segment = decode_r_u16(r);
    uint16_t length = decode_r_u16(r);
    uint8_t ord = decode_r_u8(r);
    const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));

    decode_emit_hex(fn, user, depth, "parent", parent);
    decode_emit_hex(fn, user, depth, "end", end);
    decode_emit_hex(fn, user, depth, "next", next);
    decode_emit_hex(fn, user, depth, "offset", offset);
    decode_emit_uint(fn, user, depth, "segment", segment);
    decode_emit_uint(fn, user, depth, "length", length);
    decode_emit_enum(fn, user, depth, "ordinal", ord, decode_thunk_ordinal(ord));
    decode_emit_string(fn, user, depth, "name", name);
    if (ord == 1 && r->offset + 2 <= r->size) { /* this adjustor */
        int16_t delta = decode_r_i16(r);
        decode_emit_int(fn, user, depth, "delta", delta);
    } else if (ord == 2 && r->offset + 2 <= r->size) { /* vcall */
        uint16_t table_off = decode_r_u16(r);
        decode_emit_hex(fn, user, depth, "table_offset", table_off);
    }
}

bool xx_ms_pdb_decode_symbol(uint16_t kind, const uint8_t *payload, size_t size, uint16_t machine, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    decode_reader reader;
    decode_r_init(&reader, payload, size);
    decode_reader *r = &reader;
    char str_buf[512];

    switch (kind) {
        case XX_MS_PDB_S_COMPILE:
        case XX_MS_PDB_S_COMPILE2:
        case XX_MS_PDB_S_COMPILE2_ST:
        case XX_MS_PDB_S_COMPILE3: decode_symbol_compile(kind, r, depth, fn, user); break;

        case XX_MS_PDB_S_OBJNAME:
        case XX_MS_PDB_S_OBJNAME_ST: {
            uint32_t sig = decode_r_u32(r);
            const char *name = (kind == XX_MS_PDB_S_OBJNAME_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_hex(fn, user, depth, "signature", sig);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_ENVBLOCK: {
            uint8_t flags = decode_r_u8(r);
            decode_emit_hex(fn, user, depth, "flags", flags);
            while (r->offset < r->size && !r->error) {
                const char *k = decode_r_cstring(r, str_buf, sizeof(str_buf));
                if (!k || !k[0]) break;
                char v_buf[512];
                const char *v = decode_r_cstring(r, v_buf, sizeof(v_buf));
                decode_emit_begin(fn, user, depth, "env_pair", -1, -1);
                decode_emit_string(fn, user, depth + 1, "key", k);
                decode_emit_string(fn, user, depth + 1, "value", v);
                decode_emit_end(fn, user, depth, "env_pair");
            }
            break;
        }

        case XX_MS_PDB_S_BUILDINFO: {
            uint32_t id = decode_r_u32(r);
            decode_emit_id(fn, user, depth, "build_id", id);
            break;
        }

        case XX_MS_PDB_S_GPROC32:
        case XX_MS_PDB_S_LPROC32:
        case XX_MS_PDB_S_GPROC32_ID:
        case XX_MS_PDB_S_LPROC32_ID:
        case XX_MS_PDB_S_LPROC32_DPC:
        case XX_MS_PDB_S_LPROC32_DPC_ID:
        case XX_MS_PDB_S_GPROC32_ST:
        case XX_MS_PDB_S_LPROC32_ST:
        case XX_MS_PDB_S_GPROC32_16t:
        case XX_MS_PDB_S_LPROC32_16t:
        case XX_MS_PDB_S_GPROCMIPS:
        case XX_MS_PDB_S_LPROCMIPS:
        case XX_MS_PDB_S_GPROCMIPS_ST:
        case XX_MS_PDB_S_LPROCMIPS_ST:
        case XX_MS_PDB_S_GPROCMIPS_16t:
        case XX_MS_PDB_S_LPROCMIPS_16t:
        case XX_MS_PDB_S_GPROCMIPS_ID:
        case XX_MS_PDB_S_LPROCMIPS_ID:
        case XX_MS_PDB_S_GPROCIA64:
        case XX_MS_PDB_S_LPROCIA64:
        case XX_MS_PDB_S_GPROCIA64_ST:
        case XX_MS_PDB_S_LPROCIA64_ST:
        case XX_MS_PDB_S_GPROCIA64_ID:
        case XX_MS_PDB_S_LPROCIA64_ID:
        case XX_MS_PDB_S_GPROC16:
        case XX_MS_PDB_S_LPROC16: decode_symbol_proc(kind, r, depth, fn, user); break;

        case XX_MS_PDB_S_FRAMEPROC: decode_symbol_frameproc(r, machine, depth, fn, user); break;

        case XX_MS_PDB_S_REGREL32:
        case XX_MS_PDB_S_REGREL32_ST:
        case XX_MS_PDB_S_REGREL32_16t:
        case XX_MS_PDB_S_REGREL16: {
            int32_t offset = (kind == XX_MS_PDB_S_REGREL16) ? decode_r_i16(r) : decode_r_i32(r);
            uint32_t type_index = (kind == XX_MS_PDB_S_REGREL32_16t || kind == XX_MS_PDB_S_REGREL16) ? decode_r_u16(r) : decode_r_u32(r);
            uint16_t reg = decode_r_u16(r);
            const char *name = (kind == XX_MS_PDB_S_REGREL32_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_int(fn, user, depth, "offset", offset);
            decode_emit_type(fn, user, depth, "type_index", type_index);
            decode_emit_enum(fn, user, depth, "register", reg, decode_get_register_name(machine, reg));
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_BPREL32:
        case XX_MS_PDB_S_BPREL32_ST:
        case XX_MS_PDB_S_BPREL32_16t:
        case XX_MS_PDB_S_BPREL16: {
            int32_t offset = (kind == XX_MS_PDB_S_BPREL16) ? decode_r_i16(r) : decode_r_i32(r);
            uint32_t type_index = (kind == XX_MS_PDB_S_BPREL32_16t || kind == XX_MS_PDB_S_BPREL16) ? decode_r_u16(r) : decode_r_u32(r);
            const char *name = (kind == XX_MS_PDB_S_BPREL32_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_int(fn, user, depth, "offset", offset);
            decode_emit_type(fn, user, depth, "type_index", type_index);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_REGISTER:
        case XX_MS_PDB_S_REGISTER_ST:
        case XX_MS_PDB_S_REGISTER_16t: {
            uint32_t type_index = (kind == XX_MS_PDB_S_REGISTER_16t) ? decode_r_u16(r) : decode_r_u32(r);
            uint16_t reg = decode_r_u16(r);
            const char *name = (kind == XX_MS_PDB_S_REGISTER_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_type(fn, user, depth, "type_index", type_index);
            decode_emit_enum(fn, user, depth, "register", reg, decode_get_register_name(machine, reg));
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_LOCAL:
        case XX_MS_PDB_S_LOCAL_2005: {
            uint32_t type_index = decode_r_u32(r);
            uint16_t flags = decode_r_u16(r);
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_type(fn, user, depth, "type_index", type_index);
            decode_emit_hex(fn, user, depth, "flags", flags);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_DEFRANGE_FRAMEPOINTER_REL:
        case XX_MS_PDB_S_DEFRANGE_REGISTER_REL:
        case XX_MS_PDB_S_DEFRANGE_REGISTER:
        case XX_MS_PDB_S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE:
        case XX_MS_PDB_S_DEFRANGE_SUBFIELD_REGISTER:
        case XX_MS_PDB_S_DEFRANGE_HLSL:
        case XX_MS_PDB_S_DEFRANGE_DPC_PTR_TAG:
        case XX_MS_PDB_S_DEFRANGE:
        case XX_MS_PDB_S_DEFRANGE_SUBFIELD: decode_symbol_defrange(kind, r, machine, depth, fn, user); break;

        case XX_MS_PDB_S_INLINESITE:
        case XX_MS_PDB_S_INLINESITE2: {
            uint32_t parent = decode_r_u32(r);
            uint32_t end = decode_r_u32(r);
            uint32_t inlinee = decode_r_u32(r);
            decode_emit_hex(fn, user, depth, "parent", parent);
            decode_emit_hex(fn, user, depth, "end", end);
            decode_emit_id(fn, user, depth, "inlinee", inlinee);
            if (kind == XX_MS_PDB_S_INLINESITE2) {
                uint32_t invocations = decode_r_u32(r);
                decode_emit_uint(fn, user, depth, "invocations", invocations);
            }
            decode_binary_annotations(r, depth, fn, user);
            break;
        }

        case XX_MS_PDB_S_INLINESITE_END:
        case XX_MS_PDB_S_END:
        case XX_MS_PDB_S_PROC_ID_END:
            /* No extra fields */
            break;

        case XX_MS_PDB_S_BLOCK32:
        case XX_MS_PDB_S_BLOCK32_ST:
        case XX_MS_PDB_S_BLOCK16: {
            bool is_16 = (kind == XX_MS_PDB_S_BLOCK16);
            bool is_st = (kind == XX_MS_PDB_S_BLOCK32_ST);
            uint32_t parent = is_16 ? decode_r_u16(r) : decode_r_u32(r);
            uint32_t end = is_16 ? decode_r_u16(r) : decode_r_u32(r);
            uint32_t code_size = is_16 ? decode_r_u16(r) : decode_r_u32(r);
            uint32_t offset = is_16 ? decode_r_u16(r) : decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_hex(fn, user, depth, "parent", parent);
            decode_emit_hex(fn, user, depth, "end", end);
            decode_emit_uint(fn, user, depth, "code_size", code_size);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_LABEL32:
        case XX_MS_PDB_S_LABEL32_ST:
        case XX_MS_PDB_S_LABEL16: {
            bool is_16 = (kind == XX_MS_PDB_S_LABEL16);
            bool is_st = (kind == XX_MS_PDB_S_LABEL32_ST);
            uint32_t offset = is_16 ? decode_r_u16(r) : decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            uint8_t flags = decode_r_u8(r);
            const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            decode_emit_hex(fn, user, depth, "flags", flags);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_CONSTANT:
        case XX_MS_PDB_S_CONSTANT_ST:
        case XX_MS_PDB_S_CONSTANT_16t: decode_symbol_constant(kind, r, depth, fn, user); break;

        case XX_MS_PDB_S_UDT:
        case XX_MS_PDB_S_UDT_ST:
        case XX_MS_PDB_S_UDT_16t:
        case XX_MS_PDB_S_COBOLUDT:
        case XX_MS_PDB_S_COBOLUDT_ST:
        case XX_MS_PDB_S_COBOLUDT_16t: {
            bool is_16t = (kind == XX_MS_PDB_S_UDT_16t || kind == XX_MS_PDB_S_COBOLUDT_16t);
            bool is_st = (kind == XX_MS_PDB_S_UDT_ST || kind == XX_MS_PDB_S_COBOLUDT_ST);
            uint32_t type_index = is_16t ? decode_r_u16(r) : decode_r_u32(r);
            const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_type(fn, user, depth, "type_index", type_index);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_PUB32:
        case XX_MS_PDB_S_PUB32_ST:
        case XX_MS_PDB_S_PUB32_16t:
        case XX_MS_PDB_S_PUB16: decode_symbol_pub(kind, r, depth, fn, user); break;

        case XX_MS_PDB_S_LDATA32:
        case XX_MS_PDB_S_GDATA32:
        case XX_MS_PDB_S_LDATA32_ST:
        case XX_MS_PDB_S_GDATA32_ST:
        case XX_MS_PDB_S_LDATA32_16t:
        case XX_MS_PDB_S_GDATA32_16t:
        case XX_MS_PDB_S_LDATA16:
        case XX_MS_PDB_S_GDATA16:
        case XX_MS_PDB_S_LTHREAD32:
        case XX_MS_PDB_S_GTHREAD32:
        case XX_MS_PDB_S_LTHREAD32_ST:
        case XX_MS_PDB_S_GTHREAD32_ST:
        case XX_MS_PDB_S_LTHREAD32_16t:
        case XX_MS_PDB_S_GTHREAD32_16t:
        case XX_MS_PDB_S_GDATA_HLSL:
        case XX_MS_PDB_S_LDATA_HLSL:
        case XX_MS_PDB_S_GDATA_HLSL32:
        case XX_MS_PDB_S_LDATA_HLSL32:
        case XX_MS_PDB_S_GDATA_HLSL32_EX:
        case XX_MS_PDB_S_LDATA_HLSL32_EX: decode_symbol_data(kind, r, depth, fn, user); break;

        case XX_MS_PDB_S_PROCREF:
        case XX_MS_PDB_S_LPROCREF:
        case XX_MS_PDB_S_DATAREF:
        case XX_MS_PDB_S_ANNOTATIONREF:
        case XX_MS_PDB_S_TOKENREF:
        case XX_MS_PDB_S_PROCREF_ST:
        case XX_MS_PDB_S_LPROCREF_ST:
        case XX_MS_PDB_S_DATAREF_ST: decode_symbol_ref(kind, r, depth, fn, user); break;

        case XX_MS_PDB_S_SECTION: {
            uint16_t isec = decode_r_u16(r);
            uint8_t align = decode_r_u8(r);
            decode_r_u8(r); /* reserved */
            uint32_t rva = decode_r_u32(r);
            uint32_t cb = decode_r_u32(r);
            uint32_t charact = decode_r_u32(r);
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_uint(fn, user, depth, "section", isec);
            decode_emit_uint(fn, user, depth, "alignment", align);
            decode_emit_hex(fn, user, depth, "rva", rva);
            decode_emit_uint(fn, user, depth, "length", cb);
            decode_emit_hex(fn, user, depth, "characteristics", charact);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_COFFGROUP: {
            uint32_t cb = decode_r_u32(r);
            uint32_t charact = decode_r_u32(r);
            uint32_t offset = decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_uint(fn, user, depth, "length", cb);
            decode_emit_hex(fn, user, depth, "characteristics", charact);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_EXPORT: {
            uint16_t ord = decode_r_u16(r);
            uint16_t flags = decode_r_u16(r);
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_uint(fn, user, depth, "ordinal", ord);
            decode_emit_hex(fn, user, depth, "flags", flags);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_CALLSITEINFO: {
            uint32_t offset = decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            decode_r_u16(r); /* reserved */
            uint32_t type_index = decode_r_u32(r);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            decode_emit_type(fn, user, depth, "type_index", type_index);
            break;
        }

        case XX_MS_PDB_S_HEAPALLOCSITE: {
            uint32_t offset = decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            uint16_t call_inst_size = decode_r_u16(r);
            uint32_t type_index = decode_r_u32(r);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            decode_emit_uint(fn, user, depth, "call_instruction_size", call_inst_size);
            decode_emit_type(fn, user, depth, "type_index", type_index);
            break;
        }

        case XX_MS_PDB_S_FRAMECOOKIE: {
            uint32_t code_offset = decode_r_u32(r);
            uint16_t reg = decode_r_u16(r);
            uint8_t cookie_kind = decode_r_u8(r);
            uint8_t flags = decode_r_u8(r);
            decode_emit_hex(fn, user, depth, "code_offset", code_offset);
            decode_emit_enum(fn, user, depth, "register", reg, decode_get_register_name(machine, reg));
            decode_emit_hex(fn, user, depth, "cookie_kind", cookie_kind);
            decode_emit_hex(fn, user, depth, "flags", flags);
            break;
        }

        case XX_MS_PDB_S_CALLEES:
        case XX_MS_PDB_S_CALLERS:
        case XX_MS_PDB_S_INLINEES: {
            uint32_t count = decode_r_u32(r);
            decode_emit_uint(fn, user, depth, "count", count);
            for (uint32_t i = 0; i < count && r->offset + 4 <= r->size && !r->error; i++) {
                uint32_t func_id = decode_r_u32(r);
                decode_emit_id(fn, user, depth, "func_id", func_id);
            }
            break;
        }

        case XX_MS_PDB_S_FILESTATIC: {
            uint32_t type_index = decode_r_u32(r);
            uint32_t filename_offset = decode_r_u32(r);
            uint16_t flags = decode_r_u16(r);
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_type(fn, user, depth, "type_index", type_index);
            decode_emit_hex(fn, user, depth, "mod_filename_offset", filename_offset);
            decode_emit_hex(fn, user, depth, "flags", flags);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_UNAMESPACE:
        case XX_MS_PDB_S_UNAMESPACE_ST: {
            const char *name = (kind == XX_MS_PDB_S_UNAMESPACE_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_ANNOTATION: {
            uint32_t offset = decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            while (r->offset < r->size && !r->error) {
                const char *s = decode_r_cstring(r, str_buf, sizeof(str_buf));
                if (!s || !s[0]) break;
                decode_emit_string(fn, user, depth, "annotation", s);
            }
            break;
        }

        case XX_MS_PDB_S_MANYREG:
        case XX_MS_PDB_S_MANYREG2:
        case XX_MS_PDB_S_MANYREG_ST:
        case XX_MS_PDB_S_MANYREG2_ST:
        case XX_MS_PDB_S_MANYREG_16t: {
            uint32_t type_index = (kind == XX_MS_PDB_S_MANYREG_16t) ? decode_r_u16(r) : decode_r_u32(r);
            uint16_t count = decode_r_u16(r);
            decode_emit_type(fn, user, depth, "type_index", type_index);
            decode_emit_uint(fn, user, depth, "count", count);
            for (uint16_t i = 0; i < count && r->offset + 2 <= r->size && !r->error; i++) {
                uint16_t reg = decode_r_u16(r);
                decode_emit_enum(fn, user, depth, "register", reg, decode_get_register_name(machine, reg));
            }
            bool is_st = (kind == XX_MS_PDB_S_MANYREG_ST || kind == XX_MS_PDB_S_MANYREG2_ST);
            const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_POGODATA: {
            uint32_t invocations = decode_r_u32(r);
            decode_emit_uint(fn, user, depth, "invocations", invocations);
            break;
        }

        case XX_MS_PDB_S_SEPCODE: {
            uint32_t parent = decode_r_u32(r);
            uint32_t end = decode_r_u32(r);
            uint32_t code_size = decode_r_u32(r);
            uint32_t flags = decode_r_u32(r);
            uint32_t offset = decode_r_u32(r);
            uint32_t parent_offset = decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            uint16_t parent_segment = decode_r_u16(r);
            decode_emit_hex(fn, user, depth, "parent", parent);
            decode_emit_hex(fn, user, depth, "end", end);
            decode_emit_uint(fn, user, depth, "code_size", code_size);
            decode_emit_hex(fn, user, depth, "flags", flags);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_hex(fn, user, depth, "parent_offset", parent_offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            decode_emit_uint(fn, user, depth, "parent_segment", parent_segment);
            break;
        }

        case XX_MS_PDB_S_ARMSWITCHTABLE: {
            uint32_t base_offset = decode_r_u32(r);
            uint16_t base_segment = decode_r_u16(r);
            uint16_t switch_type = decode_r_u16(r);
            uint32_t branch_offset = decode_r_u32(r);
            uint16_t branch_segment = decode_r_u16(r);
            uint32_t table_offset = decode_r_u32(r);
            uint16_t table_segment = decode_r_u16(r);
            uint32_t count = decode_r_u32(r);
            decode_emit_hex(fn, user, depth, "base_offset", base_offset);
            decode_emit_uint(fn, user, depth, "base_segment", base_segment);
            decode_emit_hex(fn, user, depth, "switch_type", switch_type);
            decode_emit_hex(fn, user, depth, "branch_offset", branch_offset);
            decode_emit_uint(fn, user, depth, "branch_segment", branch_segment);
            decode_emit_hex(fn, user, depth, "table_offset", table_offset);
            decode_emit_uint(fn, user, depth, "table_segment", table_segment);
            decode_emit_uint(fn, user, depth, "count", count);
            break;
        }

        case XX_MS_PDB_S_THUNK32:
        case XX_MS_PDB_S_THUNK32_ST:
        case XX_MS_PDB_S_THUNK16: decode_symbol_thunk(kind, r, depth, fn, user); break;

        case XX_MS_PDB_S_TRAMPOLINE: {
            uint16_t tramp_type = decode_r_u16(r);
            uint16_t tramp_size = decode_r_u16(r);
            uint32_t thunk_off = decode_r_u32(r);
            uint32_t target_off = decode_r_u32(r);
            uint16_t thunk_seg = decode_r_u16(r);
            uint16_t target_seg = decode_r_u16(r);
            decode_emit_hex(fn, user, depth, "tramp_type", tramp_type);
            decode_emit_uint(fn, user, depth, "size", tramp_size);
            decode_emit_hex(fn, user, depth, "thunk_offset", thunk_off);
            decode_emit_hex(fn, user, depth, "target_offset", target_off);
            decode_emit_uint(fn, user, depth, "thunk_segment", thunk_seg);
            decode_emit_uint(fn, user, depth, "target_segment", target_seg);
            break;
        }

        case XX_MS_PDB_S_GMANPROC:
        case XX_MS_PDB_S_LMANPROC:
        case XX_MS_PDB_S_GMANPROC_ST:
        case XX_MS_PDB_S_LMANPROC_ST: {
            uint32_t token = decode_r_u32(r);
            uint32_t offset = decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            uint8_t flags = decode_r_u8(r);
            bool is_st = (kind == XX_MS_PDB_S_GMANPROC_ST || kind == XX_MS_PDB_S_LMANPROC_ST);
            const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_hex(fn, user, depth, "token", token);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            decode_emit_hex(fn, user, depth, "flags", flags);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_MANSLOT:
        case XX_MS_PDB_S_MANSLOT_ST: {
            uint32_t slot = decode_r_u32(r);
            uint32_t type_index = decode_r_u32(r);
            const char *name = (kind == XX_MS_PDB_S_MANSLOT_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_uint(fn, user, depth, "slot", slot);
            decode_emit_type(fn, user, depth, "type_index", type_index);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_MANCONSTANT: {
            uint32_t token = decode_r_u32(r);
            decode_num_val nv;
            decode_read_numeric(r, &nv);
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_hex(fn, user, depth, "token", token);
            decode_emit_num_val(fn, user, depth, "value", &nv);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_GMANDATA:
        case XX_MS_PDB_S_LMANDATA:
        case XX_MS_PDB_S_GMANDATA_ST:
        case XX_MS_PDB_S_LMANDATA_ST: {
            uint32_t token = decode_r_u32(r);
            uint32_t offset = decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            bool is_st = (kind == XX_MS_PDB_S_GMANDATA_ST || kind == XX_MS_PDB_S_LMANDATA_ST);
            const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_hex(fn, user, depth, "token", token);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_S_HOTPATCHFUNC: {
            uint32_t offset = decode_r_u32(r);
            uint16_t segment = decode_r_u16(r);
            uint32_t type_index = decode_r_u32(r);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "segment", segment);
            decode_emit_type(fn, user, depth, "type_index", type_index);
            break;
        }

        case XX_MS_PDB_S_LOCAL_DPC_GROUPSHARED:
        case XX_MS_PDB_S_DPC_SYM_TAG_MAP: {
            uint32_t type_index = decode_r_u32(r);
            decode_emit_type(fn, user, depth, "type_index", type_index);
            break;
        }

        case XX_MS_PDB_S_FASTLINK:
        case XX_MS_PDB_S_REF_MINIPDB:
        case XX_MS_PDB_S_PDBMAP: {
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        default:
            /* Unknown symbol kind */
            decode_emit_bytes(fn, user, depth, "data", payload, size);
            return false;
    }

    decode_check_cut(r, depth, fn, user);
    return true;
}

static void decode_leaf_modifier(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char mod_buf[64];
    uint32_t type_index = (kind == XX_MS_PDB_LF_MODIFIER_16t) ? decode_r_u16(r) : decode_r_u32(r);
    uint16_t mods = decode_r_u16(r);
    mod_buf[0] = '\0';
    if (mods & 1) strcat(mod_buf, "const");
    if (mods & 2) {
        if (mod_buf[0]) strcat(mod_buf, " | ");
        strcat(mod_buf, "volatile");
    }
    if (mods & 4) {
        if (mod_buf[0]) strcat(mod_buf, " | ");
        strcat(mod_buf, "unaligned");
    }
    if (!mod_buf[0]) strcpy(mod_buf, "none");

    decode_emit_type(fn, user, depth, "type_index", type_index);
    decode_emit_flags(fn, user, depth, "modifiers", mods, mod_buf);
}

static void decode_leaf_pointer(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char attr_buf[128];
    uint32_t type_index = (kind == XX_MS_PDB_LF_POINTER_16t) ? decode_r_u16(r) : decode_r_u32(r);
    uint32_t attrs = (kind == XX_MS_PDB_LF_POINTER_16t) ? decode_r_u16(r) : decode_r_u32(r);
    uint8_t ptr_mode = (uint8_t)((attrs >> 5) & 0x07);
    uint8_t ptr_size = (uint8_t)((attrs >> 13) & 0x3F);
    const char *mode_str = "pointer";
    switch (ptr_mode) {
        case 0: mode_str = "pointer"; break;
        case 1: mode_str = "lvalue_reference"; break;
        case 2: mode_str = "member_data_pointer"; break;
        case 3: mode_str = "member_function_pointer"; break;
        case 4: mode_str = "rvalue_reference"; break;
        default: mode_str = "unknown"; break;
    }
    attr_buf[0] = '\0';
    if (attrs & 0x0100) strcat(attr_buf, "flat32");
    if (attrs & 0x0200) {
        if (attr_buf[0]) strcat(attr_buf, " | ");
        strcat(attr_buf, "volatile");
    }
    if (attrs & 0x0400) {
        if (attr_buf[0]) strcat(attr_buf, " | ");
        strcat(attr_buf, "const");
    }
    if (attrs & 0x0800) {
        if (attr_buf[0]) strcat(attr_buf, " | ");
        strcat(attr_buf, "unaligned");
    }
    if (attrs & 0x1000) {
        if (attr_buf[0]) strcat(attr_buf, " | ");
        strcat(attr_buf, "restrict");
    }
    if (!attr_buf[0]) strcpy(attr_buf, "none");

    decode_emit_type(fn, user, depth, "type_index", type_index);
    decode_emit_enum(fn, user, depth, "mode", ptr_mode, mode_str);
    decode_emit_uint(fn, user, depth, "size", ptr_size);
    decode_emit_flags(fn, user, depth, "attributes", attrs, attr_buf);

    if (ptr_mode == 2 || ptr_mode == 3) { /* Member pointer */
        uint32_t class_type = (kind == XX_MS_PDB_LF_POINTER_16t) ? decode_r_u16(r) : decode_r_u32(r);
        uint16_t repr = decode_r_u16(r);
        decode_emit_type(fn, user, depth, "containing_class", class_type);
        decode_emit_hex(fn, user, depth, "representation", repr);
    }
}

static void decode_leaf_procedure(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    bool is_16t = (kind == XX_MS_PDB_LF_PROCEDURE_16t);
    uint32_t ret_type = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    uint8_t cc = decode_r_u8(r);
    uint8_t opts = decode_r_u8(r);
    uint16_t param_count = decode_r_u16(r);
    uint32_t arg_list = is_16t ? decode_r_u16(r) : decode_r_u32(r);

    decode_emit_type(fn, user, depth, "return_type", ret_type);
    decode_emit_enum(fn, user, depth, "calling_convention", cc, decode_calling_convention(cc));
    decode_emit_hex(fn, user, depth, "options", opts);
    decode_emit_uint(fn, user, depth, "parameter_count", param_count);
    decode_emit_type(fn, user, depth, "arg_list_type", arg_list);
}

static void decode_leaf_mfunction(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    bool is_16t = (kind == XX_MS_PDB_LF_MFUNCTION_16t);
    uint32_t ret_type = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    uint32_t class_type = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    uint32_t this_type = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    uint8_t cc = decode_r_u8(r);
    uint8_t opts = decode_r_u8(r);
    uint16_t param_count = decode_r_u16(r);
    uint32_t arg_list = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    int32_t this_adjust = is_16t ? decode_r_i16(r) : decode_r_i32(r);

    decode_emit_type(fn, user, depth, "return_type", ret_type);
    decode_emit_type(fn, user, depth, "class_type", class_type);
    decode_emit_type(fn, user, depth, "this_type", this_type);
    decode_emit_enum(fn, user, depth, "calling_convention", cc, decode_calling_convention(cc));
    decode_emit_hex(fn, user, depth, "options", opts);
    decode_emit_uint(fn, user, depth, "parameter_count", param_count);
    decode_emit_type(fn, user, depth, "arg_list_type", arg_list);
    decode_emit_int(fn, user, depth, "this_adjust", this_adjust);
}

static void decode_leaf_arglist(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    bool is_16t = (kind == XX_MS_PDB_LF_ARGLIST_16t);
    uint32_t count = decode_r_u32(r);
    decode_emit_uint(fn, user, depth, "count", count);
    for (uint32_t i = 0; i < count && !r->error && (r->offset + (is_16t ? 2 : 4) <= r->size); i++) {
        uint32_t ti = is_16t ? decode_r_u16(r) : decode_r_u32(r);
        decode_emit_type(fn, user, depth, "type_index", ti);
    }
}

static void decode_leaf_substr_list(decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    uint32_t count = decode_r_u32(r);
    decode_emit_uint(fn, user, depth, "count", count);
    for (uint32_t i = 0; i < count && !r->error && (r->offset + 4 <= r->size); i++) {
        uint32_t id = decode_r_u32(r);
        decode_emit_id(fn, user, depth, "id_index", id);
    }
}

static void decode_leaf_array(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    bool is_16t = (kind == XX_MS_PDB_LF_ARRAY_16t);
    bool is_st = (kind == XX_MS_PDB_LF_ARRAY_ST);
    uint32_t elem_type = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    uint32_t idx_type = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    decode_num_val nv;
    decode_read_numeric(r, &nv);
    const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));

    decode_emit_type(fn, user, depth, "element_type", elem_type);
    decode_emit_type(fn, user, depth, "index_type", idx_type);
    decode_emit_num_val(fn, user, depth, "size", &nv);
    decode_emit_string(fn, user, depth, "name", name);
}

static void decode_leaf_udt(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    char flags_buf[256];
    bool is_v2 = (kind == XX_MS_PDB_LF_CLASS2 || kind == XX_MS_PDB_LF_STRUCTURE2 || kind == XX_MS_PDB_LF_INTERFACE2 || kind == XX_MS_PDB_LF_UNION2);
    bool is_union = (kind == XX_MS_PDB_LF_UNION || kind == XX_MS_PDB_LF_UNION2 || kind == XX_MS_PDB_LF_UNION_ST || kind == XX_MS_PDB_LF_UNION_16t);
    bool is_16t = (kind == XX_MS_PDB_LF_CLASS_16t || kind == XX_MS_PDB_LF_STRUCTURE_16t || kind == XX_MS_PDB_LF_UNION_16t);
    bool is_st = (kind == XX_MS_PDB_LF_CLASS_ST || kind == XX_MS_PDB_LF_STRUCTURE_ST || kind == XX_MS_PDB_LF_UNION_ST);

    uint16_t count = 0;
    if (!is_v2) {
        count = decode_r_u16(r);
        decode_emit_uint(fn, user, depth, "member_count", count);
    }
    uint32_t prop = is_v2 ? decode_r_u32(r) : decode_r_u16(r);
    decode_format_prop_flags(prop, flags_buf, sizeof(flags_buf));
    decode_emit_flags(fn, user, depth, "properties", prop, flags_buf);

    uint32_t fld_list = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    decode_emit_type(fn, user, depth, "field_list", fld_list);

    if (!is_union) {
        uint32_t derived = is_16t ? decode_r_u16(r) : decode_r_u32(r);
        uint32_t vshape = is_16t ? decode_r_u16(r) : decode_r_u32(r);
        decode_emit_type(fn, user, depth, "derived_list", derived);
        decode_emit_type(fn, user, depth, "vshape", vshape);
    }

    decode_num_val nv;
    decode_read_numeric(r, &nv);
    decode_emit_num_val(fn, user, depth, "size", &nv);

    const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
    decode_emit_string(fn, user, depth, "name", name);

    if ((prop & 0x0200) && r->offset < r->size) { /* has unique name */
        char ubuf[512];
        const char *uname = is_st ? decode_r_st_string(r, ubuf, sizeof(ubuf)) : decode_r_cstring(r, ubuf, sizeof(ubuf));
        decode_emit_string(fn, user, depth, "unique_name", uname);
    }
}

static void decode_leaf_enum(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    char flags_buf[256];
    bool is_16t = (kind == XX_MS_PDB_LF_ENUM_16t);
    bool is_st = (kind == XX_MS_PDB_LF_ENUM_ST);
    uint16_t count = decode_r_u16(r);
    uint16_t prop = decode_r_u16(r);
    uint32_t underlying = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    uint32_t fld_list = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    const char *name = is_st ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));

    decode_format_prop_flags(prop, flags_buf, sizeof(flags_buf));
    decode_emit_uint(fn, user, depth, "count", count);
    decode_emit_flags(fn, user, depth, "properties", prop, flags_buf);
    decode_emit_type(fn, user, depth, "underlying_type", underlying);
    decode_emit_type(fn, user, depth, "field_list", fld_list);
    decode_emit_string(fn, user, depth, "name", name);

    if ((prop & 0x0200) && r->offset < r->size) {
        char ubuf[512];
        const char *uname = is_st ? decode_r_st_string(r, ubuf, sizeof(ubuf)) : decode_r_cstring(r, ubuf, sizeof(ubuf));
        decode_emit_string(fn, user, depth, "unique_name", uname);
    }
}

static void decode_leaf_bitfield(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    bool is_16t = (kind == XX_MS_PDB_LF_BITFIELD_16t);
    uint32_t type_index = is_16t ? decode_r_u16(r) : decode_r_u32(r);
    uint8_t length = decode_r_u8(r);
    uint8_t position = decode_r_u8(r);

    decode_emit_type(fn, user, depth, "type_index", type_index);
    decode_emit_uint(fn, user, depth, "bit_size", length);
    decode_emit_uint(fn, user, depth, "bit_offset", position);
}

static void decode_leaf_vtshape(decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    uint16_t count = decode_r_u16(r);
    decode_emit_uint(fn, user, depth, "count", count);
    for (uint16_t i = 0; i < count && r->offset < r->size && !r->error; i++) {
        uint8_t byte = r->data[r->offset + (i / 2)];
        uint8_t desc = (uint8_t)((i % 2 == 0) ? (byte & 0x0F) : (byte >> 4));
        const char *desc_name = "near";
        switch (desc) {
            case 0: desc_name = "near"; break;
            case 1: desc_name = "far"; break;
            case 2: desc_name = "thin"; break;
            case 3: desc_name = "outer"; break;
            case 4: desc_name = "meta"; break;
            case 5: desc_name = "near32"; break;
            case 6: desc_name = "far32"; break;
            default: desc_name = "unknown"; break;
        }
        decode_emit_enum(fn, user, depth, "descriptor", desc, desc_name);
        if (i % 2 == 1) r->offset++;
    }
    if (count % 2 == 1 && r->offset < r->size) r->offset++;
}

static void decode_leaf_methodlist(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char attr_buf[64];
    bool is_16t = (kind == XX_MS_PDB_LF_METHODLIST_16t);
    while (r->offset < r->size && !r->error) {
        if ((r->data[r->offset] & 0xF0) == 0xF0) {
            r->offset++;
            continue;
        }
        if (r->offset + 4 > r->size) break;
        uint16_t attr = decode_r_u16(r);
        decode_r_u16(r); /* padding */
        uint32_t type_index = is_16t ? decode_r_u16(r) : decode_r_u32(r);
        uint8_t mprop = (uint8_t)((attr >> 2) & 0x07);

        decode_emit_begin(fn, user, depth, "method", -1, -1);
        decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
        decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
        decode_emit_type(fn, user, depth + 1, "type_index", type_index);
        if ((mprop == 4 || mprop == 6) && r->offset + 4 <= r->size) { /* intro / pure intro */
            uint32_t vtable_off = decode_r_u32(r);
            decode_emit_hex(fn, user, depth + 1, "vtable_offset", vtable_off);
        }
        decode_emit_end(fn, user, depth, "method");
    }
}

static void decode_leaf_fieldlist(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    char attr_buf[128];
    bool is_16t = (kind == XX_MS_PDB_LF_FIELDLIST_16t);

    while (r->offset + 2 <= r->size && !r->error) {
        /* Check padding bytes */
        if ((r->data[r->offset] & 0xF0) == 0xF0) {
            r->offset++;
            continue;
        }
        uint16_t mkind = decode_r_u16(r);
        const char *mkind_name = xx_ms_pdb_leaf_kind_to_string(mkind);
        decode_emit_begin(fn, user, depth, "member", -1, -1);
        decode_emit_enum(fn, user, depth + 1, "kind", mkind, mkind_name);

        switch (mkind) {
            case XX_MS_PDB_LF_MEMBER:
            case XX_MS_PDB_LF_MEMBER_16t:
            case XX_MS_PDB_LF_MEMBER_ST: {
                uint16_t attr = decode_r_u16(r);
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_MEMBER_16t) ? decode_r_u16(r) : decode_r_u32(r);
                decode_num_val nv;
                decode_read_numeric(r, &nv);
                const char *name = (mkind == XX_MS_PDB_LF_MEMBER_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
                decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
                decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                decode_emit_num_val(fn, user, depth + 1, "offset", &nv);
                decode_emit_string(fn, user, depth + 1, "name", name);
                break;
            }

            case XX_MS_PDB_LF_STMEMBER:
            case XX_MS_PDB_LF_STMEMBER_16t:
            case XX_MS_PDB_LF_STMEMBER_ST: {
                uint16_t attr = decode_r_u16(r);
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_STMEMBER_16t) ? decode_r_u16(r) : decode_r_u32(r);
                const char *name = (mkind == XX_MS_PDB_LF_STMEMBER_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
                decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
                decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                decode_emit_string(fn, user, depth + 1, "name", name);
                break;
            }

            case XX_MS_PDB_LF_METHOD:
            case XX_MS_PDB_LF_METHOD_16t:
            case XX_MS_PDB_LF_METHOD_ST: {
                uint16_t mcount = decode_r_u16(r);
                uint32_t mlist = (is_16t || mkind == XX_MS_PDB_LF_METHOD_16t) ? decode_r_u16(r) : decode_r_u32(r);
                const char *name = (mkind == XX_MS_PDB_LF_METHOD_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
                decode_emit_uint(fn, user, depth + 1, "count", mcount);
                decode_emit_type(fn, user, depth + 1, "method_list_type", mlist);
                decode_emit_string(fn, user, depth + 1, "name", name);
                break;
            }

            case XX_MS_PDB_LF_ONEMETHOD:
            case XX_MS_PDB_LF_ONEMETHOD_16t:
            case XX_MS_PDB_LF_ONEMETHOD_ST: {
                uint16_t attr = decode_r_u16(r);
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_ONEMETHOD_16t) ? decode_r_u16(r) : decode_r_u32(r);
                uint8_t mprop = (uint8_t)((attr >> 2) & 0x07);
                decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
                decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                if ((mprop == 4 || mprop == 6) && r->offset + 4 <= r->size) {
                    uint32_t vtab_off = decode_r_u32(r);
                    decode_emit_hex(fn, user, depth + 1, "vtable_offset", vtab_off);
                }
                const char *name = (mkind == XX_MS_PDB_LF_ONEMETHOD_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
                decode_emit_string(fn, user, depth + 1, "name", name);
                break;
            }

            case XX_MS_PDB_LF_BCLASS:
            case XX_MS_PDB_LF_BCLASS_16t: {
                uint16_t attr = decode_r_u16(r);
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_BCLASS_16t) ? decode_r_u16(r) : decode_r_u32(r);
                decode_num_val nv;
                decode_read_numeric(r, &nv);
                decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
                decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                decode_emit_num_val(fn, user, depth + 1, "offset", &nv);
                break;
            }

            case XX_MS_PDB_LF_BINTERFACE: {
                uint16_t attr = decode_r_u16(r);
                uint32_t ti = decode_r_u32(r);
                decode_num_val nv;
                decode_read_numeric(r, &nv);
                decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
                decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                decode_emit_num_val(fn, user, depth + 1, "offset", &nv);
                break;
            }

            case XX_MS_PDB_LF_VBCLASS:
            case XX_MS_PDB_LF_IVBCLASS:
            case XX_MS_PDB_LF_VBCLASS_16t:
            case XX_MS_PDB_LF_IVBCLASS_16t: {
                uint16_t attr = decode_r_u16(r);
                uint32_t base_ti = is_16t ? decode_r_u16(r) : decode_r_u32(r);
                uint32_t vbptr_ti = is_16t ? decode_r_u16(r) : decode_r_u32(r);
                decode_num_val nv_vbptr, nv_vbtable;
                decode_read_numeric(r, &nv_vbptr);
                decode_read_numeric(r, &nv_vbtable);
                decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
                decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
                decode_emit_type(fn, user, depth + 1, "base_type", base_ti);
                decode_emit_type(fn, user, depth + 1, "vbptr_type", vbptr_ti);
                decode_emit_num_val(fn, user, depth + 1, "vbptr_offset", &nv_vbptr);
                decode_emit_num_val(fn, user, depth + 1, "vbtable_index", &nv_vbtable);
                break;
            }

            case XX_MS_PDB_LF_ENUMERATE:
            case XX_MS_PDB_LF_ENUMERATE_ST: {
                uint16_t attr = decode_r_u16(r);
                decode_num_val nv;
                decode_read_numeric(r, &nv);
                const char *name = (mkind == XX_MS_PDB_LF_ENUMERATE_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
                decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
                decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
                decode_emit_num_val(fn, user, depth + 1, "value", &nv);
                decode_emit_string(fn, user, depth + 1, "name", name);
                break;
            }

            case XX_MS_PDB_LF_NESTTYPE:
            case XX_MS_PDB_LF_NESTTYPE_16t:
            case XX_MS_PDB_LF_NESTTYPE_ST: {
                decode_r_u16(r); /* padding */
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_NESTTYPE_16t) ? decode_r_u16(r) : decode_r_u32(r);
                const char *name = (mkind == XX_MS_PDB_LF_NESTTYPE_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                decode_emit_string(fn, user, depth + 1, "name", name);
                break;
            }

            case XX_MS_PDB_LF_NESTTYPEEX:
            case XX_MS_PDB_LF_NESTTYPEEX_ST: {
                uint16_t attr = decode_r_u16(r);
                uint32_t ti = decode_r_u32(r);
                const char *name =
                    (mkind == XX_MS_PDB_LF_NESTTYPEEX_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
                decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
                decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                decode_emit_string(fn, user, depth + 1, "name", name);
                break;
            }

            case XX_MS_PDB_LF_VFUNCTAB:
            case XX_MS_PDB_LF_VFUNCTAB_16t: {
                decode_r_u16(r); /* padding */
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_VFUNCTAB_16t) ? decode_r_u16(r) : decode_r_u32(r);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                break;
            }

            case XX_MS_PDB_LF_VFUNCOFF:
            case XX_MS_PDB_LF_VFUNCOFF_16t: {
                decode_r_u16(r); /* padding */
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_VFUNCOFF_16t) ? decode_r_u16(r) : decode_r_u32(r);
                uint32_t off = decode_r_u32(r);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                decode_emit_hex(fn, user, depth + 1, "offset", off);
                break;
            }

            case XX_MS_PDB_LF_INDEX:
            case XX_MS_PDB_LF_INDEX_16t: {
                decode_r_u16(r); /* padding */
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_INDEX_16t) ? decode_r_u16(r) : decode_r_u32(r);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                break;
            }

            case XX_MS_PDB_LF_FRIENDCLS:
            case XX_MS_PDB_LF_FRIENDCLS_16t: {
                decode_r_u16(r); /* padding */
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_FRIENDCLS_16t) ? decode_r_u16(r) : decode_r_u32(r);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                break;
            }

            case XX_MS_PDB_LF_FRIENDFCN:
            case XX_MS_PDB_LF_FRIENDFCN_16t:
            case XX_MS_PDB_LF_FRIENDFCN_ST: {
                decode_r_u16(r); /* padding */
                uint32_t ti = (is_16t || mkind == XX_MS_PDB_LF_FRIENDFCN_16t) ? decode_r_u16(r) : decode_r_u32(r);
                const char *name = (mkind == XX_MS_PDB_LF_FRIENDFCN_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                decode_emit_string(fn, user, depth + 1, "name", name);
                break;
            }

            case XX_MS_PDB_LF_MEMBERMODIFY:
            case XX_MS_PDB_LF_MEMBERMODIFY_ST: {
                uint16_t attr = decode_r_u16(r);
                uint32_t ti = decode_r_u32(r);
                const char *name =
                    (mkind == XX_MS_PDB_LF_MEMBERMODIFY_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
                decode_format_fldattr(attr, attr_buf, sizeof(attr_buf));
                decode_emit_flags(fn, user, depth + 1, "attributes", attr, attr_buf);
                decode_emit_type(fn, user, depth + 1, "type_index", ti);
                decode_emit_string(fn, user, depth + 1, "name", name);
                break;
            }

            default:
                /* Unknown member: stop fieldlist */
                r->error = true;
                break;
        }

        decode_emit_end(fn, user, depth, "member");
    }
}

static void decode_leaf_ipi(uint16_t kind, decode_reader *r, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    char str_buf[512];
    switch (kind) {
        case XX_MS_PDB_LF_FUNC_ID: {
            uint32_t scope = decode_r_u32(r);
            uint32_t ti = decode_r_u32(r);
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_id(fn, user, depth, "parent_scope", scope);
            decode_emit_type(fn, user, depth, "type_index", ti);
            decode_emit_string(fn, user, depth, "name", name);
            if (r->offset + 8 <= r->size) {
                uint64_t hash = decode_r_u64(r);
                decode_emit_hex(fn, user, depth, "hash", hash);
            }
            break;
        }
        case XX_MS_PDB_LF_MFUNC_ID: {
            uint32_t class_type = decode_r_u32(r);
            uint32_t ti = decode_r_u32(r);
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_type(fn, user, depth, "class_type", class_type);
            decode_emit_type(fn, user, depth, "type_index", ti);
            decode_emit_string(fn, user, depth, "name", name);
            if (r->offset + 8 <= r->size) {
                uint64_t hash = decode_r_u64(r);
                decode_emit_hex(fn, user, depth, "hash", hash);
            }
            break;
        }
        case XX_MS_PDB_LF_STRING_ID: {
            uint32_t id = decode_r_u32(r);
            const char *name = decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_id(fn, user, depth, "id", id);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }
        case XX_MS_PDB_LF_BUILDINFO: {
            uint16_t count = decode_r_u16(r);
            decode_emit_uint(fn, user, depth, "count", count);
            for (uint16_t i = 0; i < count && r->offset + 4 <= r->size && !r->error; i++) {
                uint32_t id = decode_r_u32(r);
                decode_emit_id(fn, user, depth, "id", id);
            }
            break;
        }
        case XX_MS_PDB_LF_UDT_SRC_LINE: {
            uint32_t ti = decode_r_u32(r);
            uint32_t src_id = decode_r_u32(r);
            uint32_t line = decode_r_u32(r);
            decode_emit_type(fn, user, depth, "type_index", ti);
            decode_emit_id(fn, user, depth, "source_file_id", src_id);
            decode_emit_uint(fn, user, depth, "line", line);
            break;
        }
        case XX_MS_PDB_LF_UDT_MOD_SRC_LINE: {
            uint32_t ti = decode_r_u32(r);
            uint32_t src_off = decode_r_u32(r);
            uint32_t line = decode_r_u32(r);
            uint16_t mod = decode_r_u16(r);
            decode_emit_type(fn, user, depth, "type_index", ti);
            decode_emit_hex(fn, user, depth, "source_file_offset", src_off);
            decode_emit_uint(fn, user, depth, "line", line);
            decode_emit_uint(fn, user, depth, "module", mod);
            break;
        }
        default: break;
    }
}

bool xx_ms_pdb_decode_leaf(uint16_t kind, const uint8_t *payload, size_t size, int32_t depth, xx_ms_pdb_dump_fn fn, void *user)
{
    decode_reader reader;
    decode_r_init(&reader, payload, size);
    decode_reader *r = &reader;
    char str_buf[512];

    switch (kind) {
        case XX_MS_PDB_LF_MODIFIER:
        case XX_MS_PDB_LF_MODIFIER_EX:
        case XX_MS_PDB_LF_MODIFIER_16t: decode_leaf_modifier(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_POINTER:
        case XX_MS_PDB_LF_POINTER_16t: decode_leaf_pointer(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_PROCEDURE:
        case XX_MS_PDB_LF_PROCEDURE_16t: decode_leaf_procedure(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_MFUNCTION:
        case XX_MS_PDB_LF_MFUNCTION_16t: decode_leaf_mfunction(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_ARGLIST:
        case XX_MS_PDB_LF_ARGLIST_16t: decode_leaf_arglist(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_SUBSTR_LIST: decode_leaf_substr_list(r, depth, fn, user); break;

        case XX_MS_PDB_LF_ARRAY:
        case XX_MS_PDB_LF_ARRAY_16t:
        case XX_MS_PDB_LF_ARRAY_ST:
        case XX_MS_PDB_LF_STRIDED_ARRAY: decode_leaf_array(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_CLASS:
        case XX_MS_PDB_LF_STRUCTURE:
        case XX_MS_PDB_LF_INTERFACE:
        case XX_MS_PDB_LF_UNION:
        case XX_MS_PDB_LF_CLASS2:
        case XX_MS_PDB_LF_STRUCTURE2:
        case XX_MS_PDB_LF_INTERFACE2:
        case XX_MS_PDB_LF_UNION2:
        case XX_MS_PDB_LF_CLASS_ST:
        case XX_MS_PDB_LF_STRUCTURE_ST:
        case XX_MS_PDB_LF_UNION_ST:
        case XX_MS_PDB_LF_CLASS_16t:
        case XX_MS_PDB_LF_STRUCTURE_16t:
        case XX_MS_PDB_LF_UNION_16t: decode_leaf_udt(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_ENUM:
        case XX_MS_PDB_LF_ENUM_16t:
        case XX_MS_PDB_LF_ENUM_ST: decode_leaf_enum(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_BITFIELD:
        case XX_MS_PDB_LF_BITFIELD_16t: decode_leaf_bitfield(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_VTSHAPE: decode_leaf_vtshape(r, depth, fn, user); break;

        case XX_MS_PDB_LF_VFTABLE: {
            uint32_t comp_type = decode_r_u32(r);
            uint32_t root_type = decode_r_u32(r);
            uint32_t off = decode_r_u32(r);
            uint32_t len = decode_r_u32(r);
            decode_emit_type(fn, user, depth, "complete_type", comp_type);
            decode_emit_type(fn, user, depth, "root_type", root_type);
            decode_emit_hex(fn, user, depth, "offset", off);
            decode_emit_uint(fn, user, depth, "length", len);
            while (r->offset < r->size && !r->error) {
                const char *s = decode_r_cstring(r, str_buf, sizeof(str_buf));
                if (!s || !s[0]) break;
                decode_emit_string(fn, user, depth, "name", s);
            }
            break;
        }

        case XX_MS_PDB_LF_VFTPATH:
        case XX_MS_PDB_LF_VFTPATH_16t: {
            bool is_16t = (kind == XX_MS_PDB_LF_VFTPATH_16t);
            uint32_t count = decode_r_u32(r);
            decode_emit_uint(fn, user, depth, "count", count);
            for (uint32_t i = 0; i < count && r->offset + (is_16t ? 2 : 4) <= r->size && !r->error; i++) {
                uint32_t ti = is_16t ? decode_r_u16(r) : decode_r_u32(r);
                decode_emit_type(fn, user, depth, "type_index", ti);
            }
            break;
        }

        case XX_MS_PDB_LF_LABEL: {
            uint16_t mode = decode_r_u16(r);
            decode_emit_hex(fn, user, depth, "mode", mode);
            break;
        }

        case XX_MS_PDB_LF_METHODLIST:
        case XX_MS_PDB_LF_METHODLIST_16t: decode_leaf_methodlist(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_FIELDLIST:
        case XX_MS_PDB_LF_FIELDLIST_16t: decode_leaf_fieldlist(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_TYPESERVER:
        case XX_MS_PDB_LF_TYPESERVER2:
        case XX_MS_PDB_LF_TYPESERVER_ST: {
            const uint8_t *guid = NULL;
            if (r->offset + 16 <= r->size) {
                guid = r->data + r->offset;
                r->offset += 16;
            } else {
                r->error = true;
            }
            uint32_t age = decode_r_u32(r);
            const char *name = (kind == XX_MS_PDB_LF_TYPESERVER_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_guid(fn, user, depth, "guid", guid);
            decode_emit_uint(fn, user, depth, "age", age);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_LF_PRECOMP:
        case XX_MS_PDB_LF_PRECOMP_ST:
        case XX_MS_PDB_LF_PRECOMP_16t: {
            uint32_t start_idx = (kind == XX_MS_PDB_LF_PRECOMP_16t) ? decode_r_u16(r) : decode_r_u32(r);
            uint32_t count = decode_r_u32(r);
            uint32_t sig = decode_r_u32(r);
            const char *name = (kind == XX_MS_PDB_LF_PRECOMP_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_type(fn, user, depth, "start_index", start_idx);
            decode_emit_uint(fn, user, depth, "count", count);
            decode_emit_hex(fn, user, depth, "signature", sig);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_LF_ENDPRECOMP: {
            uint32_t sig = decode_r_u32(r);
            decode_emit_hex(fn, user, depth, "signature", sig);
            break;
        }

        case XX_MS_PDB_LF_SKIP: {
            uint32_t ti = decode_r_u32(r);
            decode_emit_type(fn, user, depth, "type_index", ti);
            break;
        }

        case XX_MS_PDB_LF_FUNC_ID:
        case XX_MS_PDB_LF_MFUNC_ID:
        case XX_MS_PDB_LF_BUILDINFO:
        case XX_MS_PDB_LF_STRING_ID:
        case XX_MS_PDB_LF_UDT_SRC_LINE:
        case XX_MS_PDB_LF_UDT_MOD_SRC_LINE: decode_leaf_ipi(kind, r, depth, fn, user); break;

        case XX_MS_PDB_LF_ALIAS:
        case XX_MS_PDB_LF_ALIAS_ST: {
            uint32_t ti = decode_r_u32(r);
            const char *name = (kind == XX_MS_PDB_LF_ALIAS_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_type(fn, user, depth, "type_index", ti);
            decode_emit_string(fn, user, depth, "name", name);
            break;
        }

        case XX_MS_PDB_LF_REFSYM: {
            uint32_t offset = decode_r_u32(r);
            uint16_t mod = decode_r_u16(r);
            decode_emit_hex(fn, user, depth, "offset", offset);
            decode_emit_uint(fn, user, depth, "module", mod);
            break;
        }

        case XX_MS_PDB_LF_OEM:
        case XX_MS_PDB_LF_OEM2:
        case XX_MS_PDB_LF_OEM_16t: {
            uint16_t oem_id = decode_r_u16(r);
            uint32_t ti = (kind == XX_MS_PDB_LF_OEM_16t) ? decode_r_u16(r) : decode_r_u32(r);
            decode_emit_hex(fn, user, depth, "oem_id", oem_id);
            decode_emit_type(fn, user, depth, "type_index", ti);
            break;
        }

        case XX_MS_PDB_LF_VECTOR: {
            uint32_t elem_type = decode_r_u32(r);
            uint32_t count = decode_r_u32(r);
            decode_emit_type(fn, user, depth, "element_type", elem_type);
            decode_emit_uint(fn, user, depth, "count", count);
            break;
        }

        case XX_MS_PDB_LF_MATRIX: {
            uint32_t elem_type = decode_r_u32(r);
            uint32_t rows = decode_r_u32(r);
            uint32_t cols = decode_r_u32(r);
            decode_emit_type(fn, user, depth, "element_type", elem_type);
            decode_emit_uint(fn, user, depth, "rows", rows);
            decode_emit_uint(fn, user, depth, "columns", cols);
            break;
        }

        case XX_MS_PDB_LF_DEFARG:
        case XX_MS_PDB_LF_DEFARG_ST: {
            uint32_t ti = decode_r_u32(r);
            const char *expr = (kind == XX_MS_PDB_LF_DEFARG_ST) ? decode_r_st_string(r, str_buf, sizeof(str_buf)) : decode_r_cstring(r, str_buf, sizeof(str_buf));
            decode_emit_type(fn, user, depth, "type_index", ti);
            decode_emit_string(fn, user, depth, "expression", expr);
            break;
        }

        case XX_MS_PDB_LF_HLSL: {
            uint32_t ti = decode_r_u32(r);
            uint16_t subtype = decode_r_u16(r);
            decode_emit_type(fn, user, depth, "type_index", ti);
            decode_emit_hex(fn, user, depth, "subtype", subtype);
            break;
        }

        default:
            /* Unknown leaf kind */
            decode_emit_bytes(fn, user, depth, "data", payload, size);
            return false;
    }

    decode_check_cut(r, depth, fn, user);
    return true;
}
