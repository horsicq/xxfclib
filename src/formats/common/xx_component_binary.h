/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private checked component primitives. Concrete readers own their grammars.
 * Explicit policy variants preserve cursor, syntax, bounds and ownership rules.
 */

#ifndef XX_COMPONENT_BINARY_H

#define XX_COMPONENT_BINARY_H

#include "../xx_payload_members.h"
#include "xx_component_parser_driver.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"

typedef struct component_binary_cursor {
    const uint8_t *b;
    uint64_t p, n;
    xx_pd_struct *pd;
} component_binary_cursor;

typedef struct component_id_set {
    uint32_t *values, size, work;
} component_id_set;

static __inline uint32_t component_crc32_sized_offset(const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint32_t crc = 0U;
    uint64_t i = 0;
    while (i < n) {
        size_t part = n - i > 4096U ? 4096U : (size_t)(n - i);
        if (xx_component_parser_stopped(pd)) return 0U;
        crc = xx_crc32_calc(crc, b + (size_t)i, part);
        i += part;
    }
    return crc;
}

static __inline uint32_t component_crc32_wide_offset(const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint32_t c = 0;
    uint64_t i;
    for (i = 0; i < n;) {
        uint64_t remain = n - i;
        size_t part = (size_t)(remain > 4096 ? 4096 : remain);
        if (xx_component_parser_stopped(pd)) return 0;
        c = xx_crc32_calc(c, b + i, part);
        i += part;
    }
    return c;
}

static __inline bool component_id(component_id_set *set, uint32_t value, bool insert, xx_pd_struct *pd)
{
    uint32_t i, start = value * 2654435761U;
    if (!value) return false;
    for (i = 0; i < set->size; ++i) {
        uint32_t at = (start + i) & (set->size - 1);
        if (++set->work > 16000000 || xx_component_parser_stopped(pd)) return false;
        if (!set->values[at]) {
            if (!insert) return false;
            set->values[at] = value;
            return true;
        }
        if (set->values[at] == value) return !insert;
    }
    return false;
}

static __inline bool component_ids_init(component_id_set *set, uint32_t count)
{
    uint32_t size = 8;
    while (size < count * 2) size *= 2;
    set->size = size;
    set->work = 0;
    set->values = (uint32_t *)xx_mem_alloc((size_t)size * 4);
    if (!set->values) return false;
    xx_mem_zero(set->values, (size_t)size * 4);
    return true;
}

static __inline bool component_is_finite32(uint32_t u)
{
    return (u & 0x7f800000U) != 0x7f800000U;
}

static __inline bool component_near(double a, double b)
{
    double diff = a - b, scale = a < 0 ? -a : a, other = b < 0 ? -b : b;
    if (diff < 0) diff = -diff;
    if (other > scale) scale = other;
    return diff <= scale * 1e-12;
}

static __inline bool component_publish_memory_free_on_failure(Abstractformat *f, pm_stream *s, const char *label, uint8_t *memory, uint64_t n)
{
    pm_member *m;
    if (!memory || !n || n > 33554432 || s->count >= 4096 || !pm_add(f, s, label, 0, 0)) {
        if (memory) xx_mem_free(memory);
        return false;
    }
    m = &s->items[s->count - 1];
    m->memory = memory;
    m->size = (int64_t)n;
    m->packed_size = 0;
    return true;
}

static __inline bool component_publish_memory_keep_on_failure(Abstractformat *f, pm_stream *s, const char *t, uint8_t *bytes, uint64_t z)
{
    pm_member *m;
    if (!bytes || !z || z > 33554432 || s->count >= 4096 || !pm_add(f, s, t, 0, 0)) return false;
    m = &s->items[s->count - 1];
    m->memory = bytes;
    m->size = (int64_t)z;
    m->packed_size = 0;
    return true;
}

static __inline bool component_span(uint64_t p, uint64_t z, uint64_t n)
{
    return p <= n && z <= n - p;
}

static __inline bool component_tag(const uint8_t *p, const char *t, size_t n)
{
    return !xx_rt_memcmp(p, t, n);
}

static __inline uint32_t component_uint_be(const uint8_t *p, unsigned z)
{
    uint32_t u = 0;
    unsigned i;
    for (i = 0; i < z; ++i) u = (u << 8) | p[i];
    return u;
}

static __inline bool component_utf16_be(const uint8_t *p, uint32_t units)
{
    uint32_t i;
    for (i = 0; i < units; ++i) {
        uint16_t a = xx_data_get_u16(p + i * 2, 2, 0, true);
        if (!a) return false;
        if (a >= 0xd800 && a <= 0xdbff) {
            uint16_t b;
            if (++i == units) return false;
            b = xx_data_get_u16(p + i * 2, 2, 0, true);
            if (b < 0xdc00 || b > 0xdfff) return false;
        } else if (a >= 0xdc00 && a <= 0xdfff) return false;
    }
    return true;
}

static __inline bool component_zero(const uint8_t *p, uint64_t n)
{
    uint64_t i;
    for (i = 0; i < n; ++i)
        if (p[i]) return false;
    return true;
}

static __inline bool component_binary_take(component_binary_cursor *q, uint64_t n, const uint8_t **value)
{
    if (xx_component_parser_stopped(q->pd) || !component_span(q->p, n, q->n)) return false;
    if (value) *value = q->b + q->p;
    q->p += n;
    return true;
}

static __inline bool component_emit(Abstractformat *f, pm_stream *s, const char *t, uint64_t p, uint64_t z, uint64_t n)
{
    return z && s->count < 4096 && component_span(p, z, n) && pm_add(f, s, t, (int64_t)p, (int64_t)z);
}

static __inline int32_t component_sint_be(const uint8_t *p, unsigned z)
{
    uint32_t u = component_uint_be(p, z);
    if (z < 4 && (u & (1U << (z * 8 - 1)))) u |= ~0U << (z * 8);
    return (int32_t)u;
}

static __inline bool component_utf8(const uint8_t *b, uint64_t n, bool ascii, xx_pd_struct *pd)
{
    uint64_t p = 0;
    while (p < n) {
        uint32_t c, min;
        unsigned k, i;
        uint8_t v;
        if ((p & 4095) == 0 && xx_component_parser_stopped(pd)) return false;
        v = b[p++];
        if (v < 128) {
            if (!v || (v < 32 && v != 9 && v != 10 && v != 13)) return false;
            continue;
        }
        if (ascii) {
            return false;
        }
        if (v >= 0xc2 && v <= 0xdf) {
            k = 1;
            c = v & 31;
            min = 128;
        } else if (v >= 0xe0 && v <= 0xef) {
            k = 2;
            c = v & 15;
            min = 2048;
        } else if (v >= 0xf0 && v <= 0xf4) {
            k = 3;
            c = v & 7;
            min = 65536;
        } else return false;
        if (!component_span(p, k, n)) {
            return false;
        }
        for (i = 0; i < k; ++i) {
            if ((b[p] & 0xc0) != 0x80) return false;
            c = (c << 6) | (b[p++] & 63);
        }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}

static __inline bool component_binary_count(component_binary_cursor *q, uint32_t maximum, uint32_t *value)
{
    const uint8_t *p;
    if (!component_binary_take(q, 4, &p)) return false;
    *value = xx_data_get_u32(p, 4, 0, false);
    return *value <= maximum;
}

static __inline bool component_binary_float(component_binary_cursor *q, double *value)
{
    const uint8_t *p;
    union {
        uint32_t u;
        float f;
    } v;
    if (!component_binary_take(q, 4, &p) || !component_is_finite32(v.u = xx_data_get_u32(p, 4, 0, false))) return false;
    *value = v.f;
    return true;
}

static __inline bool component_binary_floats(component_binary_cursor *q, unsigned count)
{
    const uint8_t *p;
    unsigned i;
    if (!component_binary_take(q, (uint64_t)count * 4, &p)) return false;
    for (i = 0; i < count; ++i)
        if (!component_is_finite32(xx_data_get_u32(p + i * 4, 4, 0, false))) return false;
    return true;
}

static __inline bool component_binary_index(component_binary_cursor *q, unsigned size, bool unsign, int32_t *value)
{
    const uint8_t *p;
    uint32_t u;
    if (!component_binary_take(q, size, &p)) return false;
    u = size == 1 ? p[0] : size == 2 ? xx_data_get_u16(p, 2, 0, false) : xx_data_get_u32(p, 4, 0, false);
    if (!unsign && size < 4 && (u & (1U << (size * 8 - 1)))) u |= ~0U << (size * 8);
    if (unsign && u > 0x7fffffffU) return false;
    *value = (int32_t)u;
    return unsign || *value >= -1;
}

static __inline bool component_binary_pstring(component_binary_cursor *q)
{
    const uint8_t *p;
    uint32_t z;
    if (!component_binary_take(q, 4, &p) || (z = xx_data_get_u32(p, 4, 0, true)) > 4096 || !component_binary_take(q, (uint64_t)z * 2, &p)) return false;
    return component_utf16_be(p, z);
}

static __inline bool component_cover(Abstractformat *f, pm_stream *s, const char *label, uint64_t n)
{
    size_t i, initial = s->count;
    uint64_t covered = 0;
    for (i = 0; i < initial; ++i) {
        uint64_t start = (uint64_t)(s->items[i].offset - f->base_address), size = (uint64_t)s->items[i].packed_size;
        if (s->items[i].memory) continue;
        if (start < covered) return false;
        if (start > covered && !component_emit(f, s, label, covered, start - covered, n)) return false;
        covered = start + size;
    }
    return covered == n || component_emit(f, s, label, covered, n - covered, n);
}

#endif
