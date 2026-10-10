/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Internal bounded primitives. Every reader supplies its own complete grammar.
 */
#ifndef XX_TEXTURE_FONT_COMPONENTS_H
#define XX_TEXTURE_FONT_COMPONENTS_H
#include "../xx_payload_members.h"
#include "xx_component_parser_driver.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
static bool texture_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool texture_font_quick(Abstractformat *, uint64_t);
static __inline bool texture_font_span(uint64_t at, uint64_t bytes, uint64_t end) {
    return at <= end && bytes <= end - at;
}
static __inline bool pm_tag(const uint8_t *p, const char *tag, size_t n) { return xx_rt_memcmp(p, tag, n) == 0; }
static __inline bool texture_font_stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static __inline bool texture_font_zero(const uint8_t *p, uint64_t n) {
    uint64_t i;
    for (i = 0; i < n; ++i)
        if (p[i])
            return false;
    return true;
}
static __inline bool texture_font_finite32(const uint8_t *p) {
    return (xx_data_get_u32(p, 4, 0, false) & 0x7f800000U) != 0x7f800000U;
}
static __inline bool texture_font_emit(Abstractformat *f, pm_stream *s, const char *label, uint64_t at, uint64_t n,
                                       uint64_t end) {
    return s->count < 4096 && texture_font_span(at, n, end) && pm_add(f, s, label, (int64_t)at, (int64_t)n);
}
typedef struct texture_font_bits {
    const uint8_t *b;
    uint64_t bit, end;
} texture_font_bits;
static __inline bool texture_font_bits_get(texture_font_bits *q, unsigned count, uint32_t *v) {
    unsigned i;
    uint32_t value = 0;
    if (count > 32 || q->bit > q->end || count > q->end - q->bit)
        return false;
    for (i = 0; i < count; ++i) {
        value = (value << 1) | ((q->b[q->bit / 8] >> (7 - (unsigned)(q->bit & 7))) & 1U);
        ++q->bit;
    }
    *v = value;
    return true;
}
static __inline bool texture_font_bits_skip(texture_font_bits *q, uint64_t count) {
    if (q->bit > q->end || count > q->end - q->bit)
        return false;
    q->bit += count;
    return true;
}
static __inline uint16_t texture_font_crc16(const uint8_t *b, uint64_t n) {
    return xx_crc16(XX_CRC_TYPE_CRC16_BUYPASS, b, (size_t)n);
}
static __inline uint32_t texture_font_crc_mpeg(const uint8_t *b, uint64_t n) {
    return xx_crc32(XX_CRC_TYPE_CRC32_MPEG2, b, (size_t)n);
}
static __inline bool texture_font_probe(Abstractformat *f, uint64_t n, uint8_t *b, size_t count) {
    return count <= n && pm_read(f, 0, b, count);
}
static __inline uint32_t texture_font_uint(const uint8_t *p, unsigned bytes) {
    uint32_t n = 0;
    unsigned i;
    for (i = 0; i < bytes; ++i)
        n = (n << 8) | p[i];
    return n;
}
static __inline bool texture_font_scalar(uint32_t c) { return c <= 0x10ffffU && (c < 0xd800U || c > 0xdfffU); }
static __inline bool texture_font_utf(const uint8_t *b, uint64_t *at, uint64_t end, bool modified) {
    uint64_t p = *at;
    uint32_t c, min;
    unsigned k, i;
    uint8_t v;
    if (p >= end)
        return false;
    v = b[p++];
    if (v < 128) {
        if (modified && !v)
            return false;
        *at = p;
        return true;
    }
    if (v >= 0xc0 && v <= 0xdf) {
        k = 1;
        c = v & 31U;
        min = 128;
    } else if (v >= 0xe0 && v <= 0xef) {
        k = 2;
        c = v & 15U;
        min = 2048;
    } else if (!modified && v >= 0xf0 && v <= 0xf4) {
        k = 3;
        c = v & 7U;
        min = 65536;
    } else
        return false;
    if (!texture_font_span(p, k, end)) {
        return false;
    }
    for (i = 0; i < k; ++i) {
        if ((b[p] & 0xc0) != 0x80)
            return false;
        c = (c << 6) | (b[p++] & 63U);
    }
    if (c < min && !(modified && c == 0 && v == 0xc0 && b[p - 1] == 0x80))
        return false;
    if (!modified && !texture_font_scalar(c))
        return false;
    if (modified && c >= 0xd800 && c <= 0xdbff) {
        if (!texture_font_span(p, 3, end) || b[p] != 0xed || b[p + 1] < 0xb0 || b[p + 1] > 0xbf ||
            (b[p + 2] & 0xc0) != 0x80)
            return false;
        p += 3;
    } else if (modified && c >= 0xdc00 && c <= 0xdfff)
        return false;
    *at = p;
    return true;
}
static __inline bool texture_font_nul(const uint8_t *b, uint64_t at, uint64_t end, uint64_t *after) {
    uint64_t p = at;
    if (p >= end)
        return false;
    while (p < end && b[p])
        ++p;
    if (p == end)
        return false;
    *after = p + 1;
    return true;
}
static __inline bool texture_font_name16(const uint8_t *b, uint64_t *at, uint64_t end) {
    uint64_t p = *at, stop;
    if (!texture_font_span(p, 2, end))
        return false;
    stop = p + 2 + xx_data_get_u16(b + p, 2, 0, true);
    p += 2;
    if (stop > end || stop - p > 4096)
        return false;
    while (p < stop)
        if (!texture_font_utf(b, &p, stop, true))
            return false;
    *at = stop;
    return true;
}
static __inline bool texture_font_float_array(const uint8_t *b, uint64_t at, uint64_t count, uint64_t end,
                                              xx_pd_struct *pd) {
    uint64_t i;
    if (count > 16777216 || !texture_font_span(at, count * 4, end))
        return false;
    for (i = 0; i < count; ++i) {
        if ((i & 1023) == 0 && texture_font_stop(pd))
            return false;
        if (!texture_font_finite32(b + at + i * 4))
            return false;
    }
    return true;
}
typedef struct texture_font_text {
    const uint8_t *b;
    uint64_t p, end, start, len;
} texture_font_text;
static __inline bool texture_font_line(texture_font_text *q) {
    uint64_t p = q->p;
    q->start = p;
    while (p < q->end && q->b[p] != 10 && q->b[p] != 13) {
        if (q->b[p] < 32 && q->b[p] != 9)
            return false;
        if (q->b[p] > 126 || p - q->start > 8192)
            return false;
        ++p;
    }
    q->len = p - q->start;
    if (p < q->end && q->b[p] == 13)
        ++p;
    if (p < q->end && q->b[p] == 10)
        ++p;
    if (p == q->p)
        return false;
    q->p = p;
    return true;
}
static __inline bool texture_font_line_tag(const texture_font_text *q, const char *tag, size_t bytes) {
    return q->len >= bytes && pm_tag(q->b + q->start, tag, bytes);
}
static __inline bool texture_font_ints(const uint8_t *b, uint64_t at, uint64_t end, int32_t *v, unsigned count) {
    unsigned k;
    for (k = 0; k < count; ++k) {
        uint32_t n = 0;
        bool neg = false;
        while (at < end && (b[at] == 32 || b[at] == 9))
            ++at;
        if (at < end && b[at] == '-') {
            neg = true;
            ++at;
        }
        if (at == end || b[at] < '0' || b[at] > '9')
            return false;
        while (at < end && b[at] >= '0' && b[at] <= '9') {
            if (n > 100000000U)
                return false;
            n = n * 10 + b[at++] - '0';
        }
        if (n > 1000000000U)
            return false;
        v[k] = neg ? -(int32_t)n : (int32_t)n;
    }
    while (at < end && (b[at] == 32 || b[at] == 9))
        ++at;
    return at == end;
}
static __inline uint16_t texture_font_basis_crc(const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    uint16_t crc = UINT16_MAX;
    uint64_t i = 0;
    while (i < n) {
        size_t part = n - i > 4096U ? 4096U : (size_t)(n - i);
        if (texture_font_stop(pd))
            return 0U;
        crc = xx_crc16_ccitt_calc(crc, b + (size_t)i, part);
        i += part;
    }
    return (uint16_t)~crc;
}
XX_COMPONENT_SINGLE_READ_DRIVER(texture_font, 67108864)
#endif
