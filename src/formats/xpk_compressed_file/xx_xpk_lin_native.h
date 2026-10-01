/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 *
 * Bounded native C adaptation of Ancient's LIN1Decompressor.cpp and
 * LIN2Decompressor.cpp, revision 61cd9088a218ce43fd389f3f3c48f35fc70f7834.
 * See LICENSE.ancient for the retained upstream license.
 * LIN1/LIN3 share a byte cursor with an MSB control reservoir. LIN2/LIN4
 * add linked forward/reverse literal streams and a terminal lookup table.
 */
#ifndef XX_XPK_LIN_NATIVE_H
#define XX_XPK_LIN_NATIVE_H

#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

typedef struct xpk_lin_stream {
    const uint8_t *data;
    size_t front, back;
    uint8_t cached;
    unsigned available;
} xpk_lin_stream;

static bool xpk_lin_byte(xpk_lin_stream *stream, uint32_t *value) {
    if (stream->front >= stream->back) return false;
    *value = stream->data[stream->front++];
    return true;
}

static bool xpk_lin_bits(xpk_lin_stream *stream, unsigned count,
                         uint32_t *value) {
    uint32_t result = 0;
    unsigned index;
    if (count > 32U) return false;
    for (index = 0; index < count; ++index) {
        if (!stream->available) {
            uint32_t byte;
            if (!xpk_lin_byte(stream, &byte)) return false;
            stream->cached = (uint8_t)byte;
            stream->available = 8U;
        }
        --stream->available;
        result = (result << 1U) |
                 ((stream->cached >> stream->available) & 1U);
    }
    *value = result;
    return true;
}

static bool xpk_lin_reverse(xpk_lin_stream *stream, uint8_t *value) {
    if (stream->front >= stream->back) return false;
    *value = stream->data[--stream->back];
    return true;
}

static bool xpk_lin_nibble(xpk_lin_stream *stream, bool full_byte,
                           bool *incomplete, uint8_t *cached, uint32_t *value) {
    if (!full_byte) {
        *incomplete = !*incomplete;
        if (!*incomplete) *value = *cached & 15U;
        else {
            if (!xpk_lin_reverse(stream, cached)) return false;
            *value = *cached >> 4U;
        }
    } else if (*incomplete) {
        uint32_t low = *cached & 15U;
        if (!xpk_lin_reverse(stream, cached)) return false;
        *value = low | (*cached & 0xf0U);
    } else {
        uint8_t byte;
        if (!xpk_lin_reverse(stream, &byte)) return false;
        *value = byte;
    }
    return true;
}

static bool xpk_lin_copy(uint8_t *output, size_t *position, size_t wanted,
                         uint64_t distance, size_t count, xx_pd_struct *pd) {
    size_t index, from;
    if (!distance || distance > *position || !count) return false;
    /* Original producers can overstate the final match. */
    if (count > wanted - *position) count = wanted - *position;
    from = *position - (size_t)distance;
    for (index = 0; index < count; ++index) {
        if ((index & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
        output[(*position)++] = output[from++];
    }
    return true;
}

static bool xpk_lin1_native(const uint8_t *packed, size_t size,
                            uint8_t *output, size_t wanted, xx_pd_struct *pd) {
    xpk_lin_stream stream;
    size_t position = 0;
    uint32_t bit, count, value, distance, high;
    if (!packed || (wanted && !output) || size < 5U ||
        packed[0] || packed[1] || packed[2] || packed[3] ||
        xx_pd_is_stopped(pd)) return false;
    stream.data = packed; stream.front = 5U; stream.back = size;
    stream.cached = 0; stream.available = 0;
    while (position < wanted) {
        if (xx_pd_is_stopped(pd) || !xpk_lin_bits(&stream, 1U, &bit)) return false;
        if (!bit) {
            if (!xpk_lin_byte(&stream, &value)) return false;
            output[position++] = (uint8_t)(value ^ 0x55U);
            continue;
        }
        count = 3U;
        if (!xpk_lin_bits(&stream, 1U, &bit)) return false;
        if (bit) {
            if (!xpk_lin_bits(&stream, 2U, &count)) return false;
            if (count != 3U) count += 4U;
            else {
                if (!xpk_lin_bits(&stream, 3U, &count)) return false;
                if (count != 7U) count += 7U;
                else {
                    if (!xpk_lin_bits(&stream, 4U, &count)) return false;
                    if (count != 15U) count += 14U;
                    else {
                        if (!xpk_lin_byte(&stream, &count) || count == 255U) return false;
                        count += 3U;
                    }
                }
            }
        }
        if (!xpk_lin_bits(&stream, 2U, &value)) return false;
        high = 0U;
        if (value && !xpk_lin_bits(&stream, value * 2U, &high)) return false;
        if (!xpk_lin_byte(&stream, &distance)) return false;
        distance |= high << 8U;
        distance += value == 0U ? 1U : value == 1U ? 0x101U :
                    value == 2U ? 0x501U : 0x1501U;
        if (!xpk_lin_copy(output, &position, wanted, distance, count, pd)) return false;
    }
    return !xx_pd_is_stopped(pd);
}

static bool xpk_lin_length(xpk_lin_stream *stream, bool lin4, uint32_t *count) {
    uint32_t value, bit;
    if (!lin4) {
        if (!xpk_lin_bits(stream, 1U, &bit)) return false;
        if (!bit) { *count = 3U; return true; }
        if (!xpk_lin_bits(stream, 2U, &value)) return false;
        if (value != 3U) { *count = value + 4U; return true; }
        if (!xpk_lin_bits(stream, 3U, &value)) return false;
        *count = value == 7U ? 0U : value + 7U;
    } else {
        if (!xpk_lin_bits(stream, 2U, &value)) return false;
        if (value != 3U) { *count = value + 3U; return true; }
        if (!xpk_lin_bits(stream, 2U, &value)) return false;
        if (value != 3U) { *count = value + 6U; return true; }
        if (!xpk_lin_bits(stream, 3U, &value)) return false;
        *count = value == 7U ? 0U : value + 9U;
    }
    return true;
}

static bool xpk_lin2_native(const uint8_t *packed, size_t size,
                            uint8_t *output, size_t wanted, bool lin4,
                            xx_pd_struct *pd) {
    xpk_lin_stream control, literal;
    size_t marker, end, middle, table_size = lin4 ? 32U : 16U;
    size_t position = 0;
    uint32_t back_offset, shift, byte, bit, value, count;
    unsigned min_bits = 1U;
    bool incomplete;
    uint8_t nibble = 0;
    if (!packed || (wanted && !output) || size < 10U ||
        packed[0] || packed[1] || packed[2] || packed[3] ||
        xx_pd_is_stopped(pd)) return false;
    /* The last byte is outside the backwards marker search by design. */
    marker = size - 1U;
    while (marker) {
        --marker;
        if (packed[marker] == 255U) break;
        if ((marker & 4095U) == 0U && xx_pd_is_stopped(pd)) return false;
    }
    if (packed[marker] != 255U || marker < table_size + 1U + 10U) return false;
    end = marker - table_size - 1U;
    back_offset = ((uint32_t)packed[4] << 24U) | ((uint32_t)packed[5] << 16U) |
                  ((uint32_t)packed[6] << 8U) | packed[7];
    if (back_offset < table_size + 6U ||
        (uint64_t)back_offset + 10U > (uint64_t)end + table_size + 6U) return false;
    middle = end + table_size + 6U - back_offset;
    if (middle < 10U || middle > end || end - middle < 2U) return false;
    control.data = literal.data = packed;
    control.front = 10U; control.back = middle;
    control.cached = 0; control.available = 0;
    literal.front = middle; literal.back = end;
    literal.cached = 0; literal.available = 0;
    if (!xpk_lin_byte(&literal, &shift) || shift > 8U ||
        !xpk_lin_byte(&literal, &byte)) return false;
    literal.cached = (uint8_t)(byte >> shift);
    literal.available = 8U - shift;
    incomplete = packed[9] != 0U;
    if (incomplete && !xpk_lin_reverse(&literal, &nibble)) return false;
    while (position < wanted) {
        if (xx_pd_is_stopped(pd) || !xpk_lin_bits(&control, 1U, &bit)) return false;
        if (!bit) {
            if (!xpk_lin_bits(&literal, 1U, &bit) ||
                !xpk_lin_nibble(&literal, bit != 0U, &incomplete, &nibble, &value)) return false;
            if (!bit) {
                if (lin4) {
                    if (!xpk_lin_bits(&literal, 1U, &bit)) return false;
                    value = (value << 1U) + bit;
                }
                value = packed[end + value];
            }
            output[position++] = (uint8_t)value;
            continue;
        }
        if (!xpk_lin_length(&control, lin4, &count)) return false;
        if (!count) {
            if (!xpk_lin_bits(&control, 4U, &count)) return false;
            if (count != 15U) count += lin4 ? 16U : 14U;
            else {
                if (!xpk_lin_bits(&control, 8U, &count) || count == 255U) return false;
                count += 3U;
            }
        }
        for (;;) {
            unsigned width;
            uint64_t distance, mask;
            bool grow;
            if (!xpk_lin_bits(&control, 3U, &value)) return false;
            width = min_bits + value;
            if (width > 32U || !xpk_lin_bits(&control, width, &value)) return false;
            mask = (UINT64_C(1) << width) - 1U;
            grow = (uint64_t)value == mask && width == min_bits + 7U;
            if (grow) ++min_bits;
            if (min_bits > 32U) return false;
            distance = value + (mask & ~((UINT64_C(1) << min_bits) - 1U)) + 1U;
            if (grow) {
                if (xx_pd_is_stopped(pd)) return false;
                continue;
            }
            if (!xpk_lin_copy(output, &position, wanted, distance, count, pd)) return false;
            break;
        }
    }
    return !xx_pd_is_stopped(pd);
}

#endif
