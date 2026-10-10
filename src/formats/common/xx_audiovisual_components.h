/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Internal bounded primitives. Every reader supplies its own complete grammar.
 */
#ifndef XX_AUDIOVISUAL_COMPONENTS_H
#define XX_AUDIOVISUAL_COMPONENTS_H
#include "../xx_payload_members.h"
#include "xx_component_parser_driver.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
static bool audiovisual_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool audiovisual_quick(Abstractformat *, uint64_t);
static __inline bool audiovisual_span(uint64_t at, uint64_t bytes, uint64_t end)
{
    return at <= end && bytes <= end - at;
}
static __inline bool pm_tag(const uint8_t *p, const char *tag, size_t n)
{
    return xx_rt_memcmp(p, tag, n) == 0;
}
static __inline bool audiovisual_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static __inline bool audiovisual_zero(const uint8_t *p, uint64_t n)
{
    uint64_t i;
    for (i = 0; i < n; ++i)
        if (p[i]) return false;
    return true;
}
static __inline bool audiovisual_finite32(const uint8_t *p)
{
    return (xx_data_get_u32(p, 4, 0, false) & 0x7f800000U) != 0x7f800000U;
}
static __inline bool audiovisual_emit(Abstractformat *f, pm_stream *s, const char *label, uint64_t at, uint64_t n, uint64_t end)
{
    return s->count < 4096 && audiovisual_span(at, n, end) && pm_add(f, s, label, (int64_t)at, (int64_t)n);
}
typedef struct audiovisual_bits {
    const uint8_t *b;
    uint64_t bit, end;
} audiovisual_bits;
static __inline bool audiovisual_bits_get(audiovisual_bits *q, unsigned count, uint32_t *v)
{
    unsigned i;
    uint32_t value = 0;
    if (count > 32 || q->bit > q->end || count > q->end - q->bit) return false;
    for (i = 0; i < count; ++i) {
        value = (value << 1) | ((q->b[q->bit / 8] >> (7 - (unsigned)(q->bit & 7))) & 1U);
        ++q->bit;
    }
    *v = value;
    return true;
}
static __inline bool audiovisual_bits_skip(audiovisual_bits *q, uint64_t count)
{
    if (q->bit > q->end || count > q->end - q->bit) return false;
    q->bit += count;
    return true;
}
static __inline uint16_t audiovisual_crc16(const uint8_t *b, uint64_t n)
{
    return xx_crc16(XX_CRC_TYPE_CRC16_BUYPASS, b, (size_t)n);
}
static __inline uint32_t audiovisual_crc_mpeg(const uint8_t *b, uint64_t n)
{
    return xx_crc32(XX_CRC_TYPE_CRC32_MPEG2, b, (size_t)n);
}
static __inline bool audiovisual_probe(Abstractformat *f, uint64_t n, uint8_t *b, size_t count)
{
    return count <= n && pm_read(f, 0, b, count);
}
XX_COMPONENT_SINGLE_READ_DRIVER(audiovisual, 67108864)
#endif
