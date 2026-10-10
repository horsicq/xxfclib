/* SPDX-License-Identifier: MIT. Checked Android resource wire types. */
#ifndef XX_ANDROID_RESOURCE_WIRE_H
#define XX_ANDROID_RESOURCE_WIRE_H
#include "xx_serialized_value_helpers.h"
typedef struct android_wire_chunk {
    uint16_t type, header;
    uint64_t at, end;
} android_wire_chunk;
static XXFC_MAYBE_UNUSED bool android_wire_read(memory_blob *b, uint64_t at, uint64_t end, android_wire_chunk *c)
{
    uint32_t n;
    if (!record_span(at, 8, end) || !blob_span(b, at, 8)) return false;
    c->at = at;
    c->type = xx_data_get_u16(b->p + (size_t)at, 2, 0, false);
    c->header = xx_data_get_u16(b->p + (size_t)at + 2, 2, 0, false);
    n = xx_data_get_u32(b->p + (size_t)at + 4, 4, 0, false);
    if (c->header < 8 || c->header > n || (n & 3) || !record_span(at, n, end) || !blob_span(b, at, n)) return false;
    c->end = at + n;
    return true;
}
static bool android_wire_ref(uint32_t i, uint32_t count, bool nullable)
{
    return i < count || (nullable && i == 0xffffffffU);
}
static bool android_wire_utf16(memory_blob *b, uint64_t at, uint64_t units)
{
    uint64_t i;
    if (units > 65536 || !blob_span(b, at, units * 2)) return false;
    for (i = 0; i < units; ++i) {
        uint16_t c = xx_data_get_u16(b->p + (size_t)(at + i * 2), 2, 0, false);
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == units) return false;
            c = xx_data_get_u16(b->p + (size_t)(at + i * 2), 2, 0, false);
            if (c < 0xdc00 || c > 0xdfff) return false;
        } else if (c >= 0xdc00 && c <= 0xdfff) return false;
    }
    return true;
}
static bool android_wire_length(memory_blob *b, uint64_t *at, uint64_t end, bool utf8, uint64_t *n)
{
    if (!record_span(*at, utf8 ? 1U : 2U, end)) return false;
    if (utf8) {
        *n = b->p[(size_t)(*at)++];
        if (*n & 128) {
            if (*at >= end) return false;
            *n = ((*n & 127) << 8) | b->p[(size_t)(*at)++];
        }
    } else {
        *n = xx_data_get_u16(b->p + (size_t)*at, 2, 0, false);
        *at += 2;
        if (*n & 32768) {
            if (!record_span(*at, 2, end)) return false;
            *n = ((*n & 32767) << 16) | xx_data_get_u16(b->p + (size_t)*at, 2, 0, false);
            *at += 2;
        }
    }
    return *n <= 65536;
}
static XXFC_MAYBE_UNUSED bool android_wire_pool(memory_blob *b, android_wire_chunk *c, uint32_t *count)
{
    uint64_t at = c->at, i, table, start, limit, n, p, units, bytes;
    uint32_t styles, flags, styleStart;
    if (c->type != 1 || c->header != 28 || c->end - at < 28) return false;
    *count = xx_data_get_u32(b->p + (size_t)at + 8, 4, 0, false);
    styles = xx_data_get_u32(b->p + (size_t)at + 12, 4, 0, false);
    flags = xx_data_get_u32(b->p + (size_t)at + 16, 4, 0, false);
    start = xx_data_get_u32(b->p + (size_t)at + 20, 4, 0, false);
    styleStart = xx_data_get_u32(b->p + (size_t)at + 24, 4, 0, false);
    table = 28 + (uint64_t)*count * 4;
    if (*count > 4096 || styles || styleStart || (flags & ~257U) || start < table || !record_span(at, table, c->end) || !record_span(at, start, c->end)) return false;
    limit = c->end;
    n = c->end - at - start;
    for (i = 0; i < *count; ++i) {
        uint32_t offset = xx_data_get_u32(b->p + (size_t)(at + 28 + i * 4), 4, 0, false);
        if (offset >= n) return false;
        p = at + start + offset;
        if (!android_wire_length(b, &p, limit, (flags & 256) != 0, &units)) return false;
        if (flags & 256) {
            uint64_t actual = 0;
            if (!android_wire_length(b, &p, limit, true, &bytes) || !record_span(p, bytes + 1, limit) || b->p[(size_t)(p + bytes)] || !serialized_utf(b, p, bytes))
                return false;
            for (uint64_t j = 0; j < bytes; ++j) {
                uint8_t a = b->p[(size_t)(p + j)];
                if ((a & 192) != 128) actual += a >= 240 ? 2U : 1U;
            }
            if (actual != units) return false;
        } else if (!record_span(p, units * 2 + 2, limit) || xx_data_get_u16(b->p + (size_t)(p + units * 2), 2, 0, false) || !android_wire_utf16(b, p, units))
            return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool android_wire_value(memory_blob *b, uint64_t at, uint64_t end, uint32_t strings)
{
    uint8_t t;
    if (!record_span(at, 8, end) || !blob_span(b, at, 8) || xx_data_get_u16(b->p + (size_t)at, 2, 0, false) != 8 || b->p[(size_t)at + 2]) return false;
    t = b->p[(size_t)at + 3];
    return t <= 8 ? (t != 3 || android_wire_ref(xx_data_get_u32(b->p + (size_t)at + 4, 4, 0, false), strings, false)) : ((t >= 16 && t <= 18) || (t >= 28 && t <= 31));
}
#endif
