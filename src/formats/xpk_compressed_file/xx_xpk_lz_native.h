/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 *
 * Bounded native C adaptations of Ancient RDCNDecompressor.cpp and
 * ILZRDecompressor.cpp, revision61cd9088a218ce43fd389f3f3c48f35fc70f7834.
 * See LICENSE.ancient. Both are allocation-free and reset per packed chunk.
 */
#ifndef XX_XPK_LZ_NATIVE_H
#define XX_XPK_LZ_NATIVE_H
#include "xx_xpk_lin_native.h"

static bool xpk_rdcn_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    xpk_lin_stream stream;
    size_t position = 0;
    uint32_t flags = 0;
    unsigned available = 0;
    if ((!packed && size) || (wanted && !output) || xx_pd_is_stopped(pd)) return false;
    stream.data = packed;
    stream.front = 0;
    stream.back = size;
    stream.cached = 0;
    stream.available = 0;
    while (position < wanted) {
        uint32_t command, low, count, distance, byte;
        if (xx_pd_is_stopped(pd)) return false;
        if (!available) {
            uint32_t high;
            if (!xpk_lin_byte(&stream, &high) || !xpk_lin_byte(&stream, &low)) return false;
            flags = (high << 8U) | low;
            available = 16U;
        }
        --available;
        if (!xpk_lin_byte(&stream, &byte)) return false;
        if (!((flags >> available) & 1U)) {
            output[position++] = (uint8_t)byte;
            continue;
        }
        command = byte >> 4U;
        count = byte & 15U;
        if (command < 2U) {
            size_t index;
            if (command == 1U) {
                if (!xpk_lin_byte(&stream, &low)) return false;
                count |= low << 4U;
            }
            count += command ? 19U : 3U;
            if (count > wanted - position || !xpk_lin_byte(&stream, &byte)) return false;
            for (index = 0; index < count; ++index) {
                if ((index & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
                output[position++] = (uint8_t)byte;
            }
        } else {
            if (!xpk_lin_byte(&stream, &low)) return false;
            distance = (count | (low << 4U)) + 3U;
            count = command;
            if (command == 2U) {
                if (!xpk_lin_byte(&stream, &count)) return false;
                count += 16U;
            }
            if (count > wanted - position || !xpk_lin_copy(output, &position, wanted, distance, count, pd)) return false;
        }
    }
    return !xx_pd_is_stopped(pd);
}

static bool xpk_ilzr_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    xpk_lin_stream stream;
    size_t position = 0;
    unsigned width = 8U;
    if (!packed || !output || size < 2U || !wanted || wanted > 65535U || (((size_t)packed[0] << 8U) | packed[1]) != wanted || xx_pd_is_stopped(pd)) return false;
    stream.data = packed;
    stream.front = 2U;
    stream.back = size;
    stream.cached = 0;
    stream.available = 0;
    while (position < wanted) {
        uint32_t bit, value, count;
        if (xx_pd_is_stopped(pd) || !xpk_lin_bits(&stream, 1U, &bit)) return false;
        if (bit) {
            if (!xpk_lin_bits(&stream, 8U, &value)) return false;
            output[position++] = (uint8_t)value;
        } else {
            while (position > ((size_t)1U << width)) ++width;
            if (!xpk_lin_bits(&stream, width, &value) || value >= position || !xpk_lin_bits(&stream, 4U, &count)) return false;
            count += 3U;
            if (count > wanted - position || !xpk_lin_copy(output, &position, wanted, position - value, count, pd)) return false;
        }
    }
    return !xx_pd_is_stopped(pd);
}
#endif
