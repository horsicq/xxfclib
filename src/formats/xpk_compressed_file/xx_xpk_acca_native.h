/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient ACCADecompressor.cpp,
 * commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_ACCA_NATIVE_H
#define XX_XPK_ACCA_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct xpk_acca_bits {
    const uint8_t *data;
    size_t size, at;
    uint16_t flags;
    unsigned left;
} xpk_acca_bits;
static bool xpk_acca_bit(xpk_acca_bits *s, unsigned *value)
{
    if (!s->left) {
        if (s->at > s->size || s->size - s->at < 2U) return false;
        s->flags = (uint16_t)(((uint16_t)s->data[s->at] << 8U) | s->data[s->at + 1U]);
        s->at += 2U;
        s->left = 16U;
    }
    --s->left;
    *value = (s->flags >> s->left) & 1U;
    return true;
}
static bool xpk_acca_byte(xpk_acca_bits *s, uint8_t *value)
{
    if (s->at >= s->size) return false;
    *value = s->data[s->at++];
    return true;
}
/* Version 20=ACCA. The caller provides the XPK chunk's exact raw size. */
static bool xpk_acca_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    static const uint8_t static_bytes[16] = {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x55U, 0x60U, 0x80U, 0xaaU, 0xc0U, 0xe0U, 0xffU};
    xpk_acca_bits s;
    size_t produced = 0U;
    if (!packed || (wanted && !output) || xx_pd_is_stopped(pd)) return false;
    s.data = packed;
    s.size = size;
    s.at = 0U;
    s.flags = 0U;
    s.left = 0U;
    while (produced < wanted) {
        unsigned control, code;
        uint8_t tmp;
        uint32_t count, distance = 0U;
        uint8_t repeat = 0U;
        bool rle = false;
        size_t i;
        if (xx_pd_is_stopped(pd) || !xpk_acca_bit(&s, &control)) return false;
        if (!control) {
            if (!xpk_acca_byte(&s, &output[produced])) return false;
            ++produced;
            continue;
        }
        if (!xpk_acca_byte(&s, &tmp)) return false;
        count = tmp & 15U;
        code = tmp >> 4U;
        switch (code) {
            case 0U:
                if (!xpk_acca_byte(&s, &repeat)) return false;
                count += 3U;
                rle = true;
                break;
            case 14U:
                count += 3U;
                rle = true;
                break;
            case 1U:
                if (!xpk_acca_byte(&s, &tmp)) return false;
                count |= (uint32_t)tmp << 4U;
                count += 19U;
                if (!xpk_acca_byte(&s, &repeat)) return false;
                rle = true;
                break;
            case 2U:
                repeat = static_bytes[count];
                count = 2U;
                rle = true;
                break;
            case 15U:
                if (!xpk_acca_byte(&s, &tmp)) return false;
                distance = (count | ((uint32_t)tmp << 4U)) + 3U;
                if (!xpk_acca_byte(&s, &tmp)) return false;
                count = (uint32_t)tmp + 14U;
                break;
            default:
                distance = count;
                if (!xpk_acca_byte(&s, &tmp)) return false;
                distance = (distance | ((uint32_t)tmp << 4U)) + 3U;
                count = code;
                break;
        }
        if (count > wanted - produced || (!rle && (!distance || distance > produced))) return false;
        if (rle) {
            memset(output + produced, repeat, count);
            produced += count;
        } else {
            for (i = 0U; i < count; ++i) output[produced + i] = output[produced + i - distance];
            produced += count;
        }
    }
    return !xx_pd_is_stopped(pd);
}
#endif
