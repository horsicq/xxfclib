/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 *
 * Bounded native C adaptation of Ancient's TDCSDecompressor.cpp, revision
 * 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_TDCS_NATIVE_H
#define XX_XPK_TDCS_NATIVE_H

#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

typedef struct xpk_tdcs_bits_s {
    const uint8_t *data;
    size_t size;
    size_t cursor;
    uint32_t word;
    unsigned remaining;
} xpk_tdcs_bits;

static bool xpk_tdcs_control(xpk_tdcs_bits *bits, unsigned *control)
{
    if (!bits->remaining) {
        if (bits->cursor > bits->size || bits->size - bits->cursor < 4U) return false;
        bits->word = ((uint32_t)bits->data[bits->cursor] << 24U) | ((uint32_t)bits->data[bits->cursor + 1U] << 16U) | ((uint32_t)bits->data[bits->cursor + 2U] << 8U) |
                     (uint32_t)bits->data[bits->cursor + 3U];
        bits->cursor += 4U;
        bits->remaining = 16U;
    }
    bits->remaining--;
    *control = (unsigned)((bits->word >> (bits->remaining * 2U)) & 3U);
    return true;
}

static bool xpk_tdcs_byte(xpk_tdcs_bits *bits, uint8_t *value)
{
    if (bits->cursor >= bits->size) return false;
    *value = bits->data[bits->cursor++];
    return true;
}

static bool xpk_tdcs_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    xpk_tdcs_bits bits;
    size_t position = 0U;
    if (!packed || (wanted && !output) || xx_pd_is_stopped(pd)) return false;
    bits.data = packed;
    bits.size = size;
    bits.cursor = 0U;
    bits.word = 0U;
    bits.remaining = 0U;
    while (position < wanted) {
        unsigned control;
        uint8_t hi, lo;
        uint32_t word, distance, count;
        size_t i;
        if ((position & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
        if (!xpk_tdcs_control(&bits, &control)) return false;
        if (control == 0U) {
            if (!xpk_tdcs_byte(&bits, &output[position])) return false;
            position++;
            continue;
        }
        if (!xpk_tdcs_byte(&bits, &hi) || !xpk_tdcs_byte(&bits, &lo)) return false;
        word = ((uint32_t)hi << 8U) | (uint32_t)lo;
        if (control == 1U) {
            count = (word & 3U) + 3U;
            distance = ((word >> 2U) ^ 0x3fffU) + 1U;
        } else if (control == 2U) {
            count = (word & 15U) + 3U;
            distance = ((word >> 4U) ^ 0xfffU) + 1U;
        } else {
            if (!word || !xpk_tdcs_byte(&bits, &lo)) return false;
            distance = (word ^ 0xffffU) + 1U;
            count = (uint32_t)lo + 3U;
        }
        if (!distance || distance > position || count > wanted - position) return false;
        for (i = 0U; i < count; ++i) {
            if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
            output[position] = output[position - distance];
            position++;
        }
    }
    return !xx_pd_is_stopped(pd);
}

#endif
