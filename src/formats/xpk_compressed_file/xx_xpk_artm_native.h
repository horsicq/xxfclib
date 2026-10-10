/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient ARTMDecompressor.cpp,
 * RangeDecoder.cpp and FrequencyTree.hpp at commit
 * 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_ARTM_NATIVE_H
#define XX_XPK_ARTM_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

typedef struct xpk_artm_bits {
    const uint8_t *data;
    size_t size, at;
    unsigned pad;
    uint8_t value;
    unsigned left;
} xpk_artm_bits;
typedef struct xpk_artm_range {
    uint16_t low, high, stream;
} xpk_artm_range;
static bool xpk_artm_bit(xpk_artm_bits *bits, uint32_t *bit)
{
    if (!bits->left) {
        if (bits->at < bits->size) bits->value = bits->data[bits->at++];
        else if (bits->pad) {
            bits->value = 0U;
            --bits->pad;
        } else return false;
        bits->left = 8U;
    }
    *bit = bits->value & 1U;
    bits->value >>= 1U;
    --bits->left;
    return true;
}
static bool xpk_artm_value(const xpk_artm_range *range, uint16_t total, uint16_t *value)
{
    uint32_t interval, numerator;
    if (!total || range->high < range->low || range->stream < range->low || range->stream > range->high) return false;
    interval = (uint32_t)range->high - range->low + 1U;
    numerator = ((uint32_t)range->stream - range->low + 1U) * total - 1U;
    *value = (uint16_t)(numerator / interval);
    return *value < total;
}
static bool xpk_artm_scale(xpk_artm_range *range, xpk_artm_bits *bits, uint16_t low, uint16_t high, uint16_t total)
{
    uint32_t interval;
    if (!total || low >= high || high > total || range->high < range->low) return false;
    interval = (uint32_t)range->high - range->low + 1U;
    range->high = (uint16_t)((interval * high) / total + range->low - 1U);
    range->low = (uint16_t)((interval * low) / total + range->low);
    if (range->high < range->low) return false;
    for (;;) {
        uint16_t decr;
        uint32_t bit;
        if (range->high < 0x8000U) decr = 0U;
        else if (range->low >= 0x8000U) decr = 0x8000U;
        else if (range->low >= 0x4000U && range->high < 0xc000U) decr = 0x4000U;
        else break;
        if (!xpk_artm_bit(bits, &bit)) return false;
        range->low = (uint16_t)((range->low - decr) << 1U);
        range->high = (uint16_t)(((range->high - decr) << 1U) | 1U);
        range->stream = (uint16_t)(((range->stream - decr) << 1U) | bit);
    }
    return true;
}
/* Version 21=ARTM. The caller provides the exact XPK chunk raw size. */
static bool xpk_artm_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    xpk_artm_bits bits;
    xpk_artm_range range;
    uint16_t freq[257], total = 257U;
    uint8_t chars[257];
    size_t out, i;
    uint32_t bit, initial = 0U;
    if (!packed || size < 2U || (wanted && !output) || xx_pd_is_stopped(pd)) return false;
    bits.data = packed;
    bits.size = size;
    bits.at = 0U;
    bits.pad = 3U;
    bits.value = 0U;
    bits.left = 0U;
    for (i = 0U; i < 16U; ++i) {
        if (!xpk_artm_bit(&bits, &bit)) return false;
        initial |= bit << i;
    }
    range.low = 0U;
    range.high = 0xffffU;
    range.stream = 0U;
    for (i = 0U; i < 16U; ++i) range.stream = (uint16_t)((range.stream << 1U) | ((initial >> i) & 1U));
    for (i = 0U; i < 257U; ++i) {
        freq[i] = 1U;
        chars[i] = (uint8_t)(256U - i);
    }
    for (out = 0U; out < wanted; ++out) {
        uint16_t value, low = 0U, symbol = 257U, k, old_total = total;
        if (xx_pd_is_stopped(pd) || !xpk_artm_value(&range, total, &value)) return false;
        for (k = 0U; k < 257U; ++k) {
            if (value < (uint16_t)(low + freq[k])) {
                symbol = k;
                break;
            }
            low = (uint16_t)(low + freq[k]);
        }
        if (symbol == 0U || symbol >= 257U || !freq[symbol]) return false;
        if (!xpk_artm_scale(&range, &bits, low, (uint16_t)(low + freq[symbol]), old_total)) return false;
        output[out] = chars[symbol];
        if (total == 0x3fffU) {
            total = 0U;
            for (k = 1U; k <= 256U; ++k) freq[k] = (uint16_t)((freq[k] + 1U) >> 1U);
            for (k = 0U; k < 257U; ++k) total = (uint16_t)(total + freq[k]);
        }
        for (k = symbol; k < 256U && freq[k + 1U] == freq[k]; ++k) {
        }
        if (k != symbol) {
            uint8_t temp = chars[symbol];
            chars[symbol] = chars[k];
            chars[k] = temp;
        }
        ++freq[k];
        ++total;
    }
    return !xx_pd_is_stopped(pd);
}
#endif
