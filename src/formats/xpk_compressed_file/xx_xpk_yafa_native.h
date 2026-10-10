/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient LZW2/LZW3/LZW4/LZW5 and LZBS,
 * commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_YAFA_NATIVE_H
#define XX_XPK_YAFA_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

typedef struct xpk_yafa_stream {
    const uint8_t *data;
    size_t size, at;
    uint32_t control;
    unsigned available;
} xpk_yafa_stream;
static bool xpk_yafa_byte(xpk_yafa_stream *s, uint32_t *value)
{
    if (s->at >= s->size) return false;
    *value = s->data[s->at++];
    return true;
}
static bool xpk_yafa_control(xpk_yafa_stream *s, bool msb, unsigned count, uint32_t *value)
{
    uint32_t result = 0U;
    unsigned i;
    if (count > 32U) return false;
    for (i = 0U; i < count; ++i) {
        uint32_t bit;
        if (!s->available) {
            if (s->size - s->at < 4U) return false;
            s->control = ((uint32_t)s->data[s->at] << 24U) | ((uint32_t)s->data[s->at + 1U] << 16U) | ((uint32_t)s->data[s->at + 2U] << 8U) | s->data[s->at + 3U];
            s->at += 4U;
            s->available = 32U;
        }
        --s->available;
        bit = msb ? ((s->control >> s->available) & 1U) : (s->control & 1U);
        if (!msb) s->control >>= 1U;
        result = msb ? (result << 1U) | bit : result | (bit << i);
    }
    *value = result;
    return true;
}
static bool xpk_yafa_copy(uint8_t *output, size_t *at, size_t wanted, uint32_t distance, uint32_t count, xx_pd_struct *pd)
{
    size_t i, source;
    if (!distance || distance > *at || count > wanted - *at) return false;
    source = *at - distance;
    for (i = 0U; i < count; ++i) {
        if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
        output[(*at)++] = output[source++];
    }
    return true;
}
/* Version 13=LZW2, 14=LZW3, 15=LZW4, 16=LZW5. */
static bool xpk_yafa_lzw_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, unsigned version, xx_pd_struct *pd)
{
    xpk_yafa_stream s = {packed, size, 0U, 0U, 0U};
    size_t at = 0U;
    bool msb = version >= 15U;
    if (!packed || (wanted && !output) || version < 13U || version > 16U || xx_pd_is_stopped(pd)) return false;
    while (at < wanted) {
        uint32_t command, high, low, packed_distance, distance, count;
        if (xx_pd_is_stopped(pd) || !xpk_yafa_control(&s, msb, version == 16U ? 2U : 1U, &command)) return false;
        if (command == 0U) {
            if (!xpk_yafa_byte(&s, &low)) return false;
            output[at++] = (uint8_t)low;
            continue;
        }
        if (!xpk_yafa_byte(&s, &high) || !xpk_yafa_byte(&s, &low)) return false;
        packed_distance = (high << 8U) | low;
        if (!packed_distance) return false;
        if (version == 16U && command == 1U) {
            count = (packed_distance & 3U) + 2U;
            distance = 0x4000U - (packed_distance >> 2U);
        } else if (version == 16U && command == 2U) {
            count = (packed_distance & 15U) + 2U;
            distance = 0x1000U - (packed_distance >> 4U);
        } else {
            if (!xpk_yafa_byte(&s, &low)) return false;
            count = low + (version == 15U ? 3U : version == 16U ? 3U : 4U);
            distance = 0x10000U - packed_distance;
        }
        if (!xpk_yafa_copy(output, &at, wanted, distance, count, pd)) return false;
    }
    return !xx_pd_is_stopped(pd);
}
typedef struct xpk_yafa_bits8 {
    const uint8_t *data;
    size_t size, at;
    uint8_t current;
    unsigned available;
} xpk_yafa_bits8;
static bool xpk_yafa_reversed_bits(xpk_yafa_bits8 *s, unsigned count, uint32_t *value)
{
    uint32_t result = 0U;
    unsigned i;
    if (count > 16U) return false;
    for (i = 0U; i < count; ++i) {
        uint32_t bit;
        if (!s->available) {
            if (s->at >= s->size) return false;
            s->current = s->data[s->at++];
            s->available = 8U;
        }
        --s->available;
        bit = (s->current >> s->available) & 1U;
        result |= bit << i;
    }
    *value = result;
    return true;
}
/* Version 17=LZBS. */
static bool xpk_yafa_lzbs_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    xpk_yafa_bits8 s = {packed, size, 1U, 0U, 0U};
    size_t at = 0U;
    unsigned bits = 0U, max_bits;
    if (!packed || size < 1U || (wanted && !output) || xx_pd_is_stopped(pd)) return false;
    max_bits = packed[0];
    if (max_bits > 31U) return false;
    while (at < wanted) {
        uint32_t command, value, count, distance;
        if (xx_pd_is_stopped(pd) || !xpk_yafa_reversed_bits(&s, 1U, &command)) return false;
        if (!command) {
            if (!xpk_yafa_reversed_bits(&s, 8U, &value)) return false;
            output[at++] = (uint8_t)value;
            continue;
        }
        if (!xpk_yafa_reversed_bits(&s, 8U, &count)) return false;
        count += 2U;
        if (count == 2U) {
            size_t i;
            if (!xpk_yafa_reversed_bits(&s, 12U, &count) || !count || count > wanted - at) return false;
            for (i = 0U; i < count; ++i) {
                if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
                if (!xpk_yafa_reversed_bits(&s, 8U, &value)) return false;
                output[at++] = (uint8_t)value;
            }
        } else {
            while (bits < max_bits && at >= ((size_t)1U << bits)) ++bits;
            if (!xpk_yafa_reversed_bits(&s, bits, &distance) || !xpk_yafa_copy(output, &at, wanted, distance, count, pd)) return false;
        }
    }
    return !xx_pd_is_stopped(pd);
}
#endif
