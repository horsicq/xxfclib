/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptations of Ancient's FBR2 and SLZ3 XPK decompressors,
 * revision 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_CYBER_NATIVE_H
#define XX_XPK_CYBER_NATIVE_H

#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

static bool xpk_fbr2_native(const uint8_t *packed, size_t size,
                            uint8_t *output, size_t wanted, xx_pd_struct *pd) {
    size_t cursor = 1U, position = 0U;
    uint8_t mode;
    if (!packed || size < 1U || (wanted && !output) || xx_pd_is_stopped(pd))
        return false;
    mode = packed[0];
    if (mode != 33U && mode != 67U && mode != 100U) return false;
    while (position < wanted) {
        uint32_t control = 0U, count;
        size_t width = mode == 33U ? 4U : mode == 67U ? 2U : 1U;
        size_t i;
        bool literal;
        if (xx_pd_is_stopped(pd) || cursor > size || size - cursor < width)
            return false;
        for (i = 0U; i < width; ++i)
            control = (control << 8U) | packed[cursor++];
        if (mode == 33U) {
            literal = control >= UINT32_C(0x80000000);
            count = literal ? 0U - control : control;
        } else if (mode == 67U) {
            literal = control >= 0x8000U;
            count = literal ? 0x10000U - control : control;
        } else {
            literal = control >= 0x80U;
            count = literal ? 0x100U - control : control;
        }
        count++;
        if ((size_t)count > wanted - position) return false;
        if (literal) {
            if (cursor > size || (size_t)count > size - cursor) return false;
            for (i = 0U; i < (size_t)count; ++i) {
                if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
                output[position++] = packed[cursor++];
            }
        } else {
            uint8_t value;
            if (cursor >= size) return false;
            value = packed[cursor++];
            for (i = 0U; i < (size_t)count; ++i) {
                if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
                output[position++] = value;
            }
        }
    }
    return !xx_pd_is_stopped(pd);
}

static bool xpk_slz3_native(const uint8_t *packed, size_t size,
                            uint8_t *output, size_t wanted, xx_pd_struct *pd) {
    size_t cursor = 0U, position = 0U;
    uint8_t flags = 0U;
    unsigned remaining = 0U;
    if (!packed || (wanted && !output) || xx_pd_is_stopped(pd)) return false;
    while (position < wanted) {
        unsigned bit;
        if ((position & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
        if (!remaining) {
            if (cursor >= size) return false;
            flags = packed[cursor++];
            remaining = 8U;
        }
        remaining--;
        bit = (unsigned)((flags >> remaining) & 1U);
        if (!bit) {
            if (cursor >= size) return false;
            output[position++] = packed[cursor++];
        } else {
            uint8_t token;
            size_t distance, count, i;
            if (cursor > size || size - cursor < 2U) return false;
            token = packed[cursor++];
            if (!token) return false;
            distance = ((size_t)(token & 0xf0U) << 4U) | packed[cursor++];
            count = (size_t)(token & 15U) + 2U;
            if (!distance || distance > position || count > wanted - position)
                return false;
            for (i = 0U; i < count; ++i) {
                if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
                output[position] = output[position - distance];
                position++;
            }
        }
    }
    return !xx_pd_is_stopped(pd);
}

#endif
