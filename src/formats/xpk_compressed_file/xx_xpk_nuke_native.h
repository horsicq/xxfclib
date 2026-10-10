/* Copyright (c) 2017-2026 Teemu Suutari
 * Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Bounded native C adaptation of Ancient NUKEDecompressor.cpp, InputStream
 * bit readers, VariableLengthCodeDecoder and DLTADecode (see LICENSE.ancient).
 * https://github.com/temisu/ancient/blob/master/src/NUKEDecompressor.cpp
 *
 * Four independent cached bit reservoirs consume big-endian words from a
 * shared front cursor, while literal bytes consume the back cursor. Each
 * physical read checks both cursors; cached bits are usable after they meet.
 * Chunk-local history/delta state, exact output bounds and no allocation.
 */
#ifndef XX_XPK_NUKE_NATIVE_PRIVATE_H
#define XX_XPK_NUKE_NATIVE_PRIVATE_H

#include "xxfclib/data/xx_pd.h"

typedef struct xpk_nuke_reservoir_s {
    uint32_t word;
    unsigned remaining;
} xpk_nuke_reservoir;
typedef struct xpk_nuke_input_s {
    const uint8_t *bytes;
    size_t front, back;
    xpk_nuke_reservoir bits[4];
    xx_pd_struct *pd;
    bool failed;
} xpk_nuke_input;

static bool xpk_nuke_cancelled(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
/* Slots0/1/3 refill an MSB BE16 word; slot2 refills an LSB BE32 word. */
static uint32_t xpk_nuke_read(xpk_nuke_input *input, unsigned slot, unsigned count)
{
    xpk_nuke_reservoir *reservoir = &input->bits[slot];
    uint32_t value = 0U;
    unsigned position = 0U;
    if (input->failed || xpk_nuke_cancelled(input->pd)) {
        input->failed = true;
        return 0U;
    }
    while (count) {
        unsigned take;
        uint32_t mask, part;
        if (!reservoir->remaining) {
            unsigned width = slot == 2U ? 4U : 2U, i;
            if (input->front > input->back || width > input->back - input->front || xpk_nuke_cancelled(input->pd)) {
                input->failed = true;
                return 0U;
            }
            reservoir->word = 0U;
            for (i = 0U; i < width; ++i) reservoir->word = (reservoir->word << 8U) | input->bytes[input->front++];
            reservoir->remaining = width * 8U;
        }
        take = count < reservoir->remaining ? count : reservoir->remaining;
        /* Callers read at most14 bits (or a nibble), so shifts remain<32. */
        mask = (UINT32_C(1) << take) - 1U;
        if (slot == 2U) {
            part = reservoir->word & mask;
            value |= part << position;
            reservoir->word >>= take;
            reservoir->remaining -= take;
            position += take;
        } else {
            reservoir->remaining -= take;
            part = (reservoir->word >> reservoir->remaining) & mask;
            value = (value << take) | part;
        }
        count -= take;
    }
    return value;
}

/* Packed and output buffers must be separate. DUKE applies byte deltas after
 * the identical NUKE LZ stream succeeds. Trailing cached/slack input follows
 * producer behavior; the outer XPK layer verifies its checksums. */
static bool xpk_nuke_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, bool duke, xx_pd_struct *pd)
{
    static const uint8_t distance_bits[16] = {4, 6, 8, 9, 4, 7, 9, 11, 13, 14, 5, 7, 9, 11, 13, 14};
    static const uint16_t distance_base[16] = {0, 16, 80, 336, 0, 16, 144, 656, 2704, 10896, 0, 32, 160, 672, 2720, 10912};
    xpk_nuke_input input = {0};
    size_t produced = 0U;
    if (!packed || (wanted && !output) || xpk_nuke_cancelled(pd)) return false;
    input.bytes = packed;
    input.back = size;
    input.pd = pd;
    for (;;) {
        uint32_t index, distance;
        size_t count, remaining;
        if (xpk_nuke_cancelled(pd)) return false;
        if (!xpk_nuke_read(&input, 0U, 1U)) {
            count = 0U;
            remaining = wanted - produced;
            if (xpk_nuke_read(&input, 0U, 1U)) count = 1U;
            else {
                uint32_t code;
                do {
                    size_t increment;
                    code = xpk_nuke_read(&input, 1U, 2U);
                    increment = code ? 5U - code : 3U;
                    if (input.failed || count > remaining || increment > remaining - count) return false;
                    count += increment;
                } while (!code);
            }
            if (input.failed || count > remaining) return false;
            while (count--) {
                if (input.front >= input.back || ((produced & 4095U) == 0U && xpk_nuke_cancelled(pd))) return false;
                output[produced++] = input.bytes[--input.back];
            }
        }
        if (input.failed) return false;
        if (produced == wanted) break;
        index = xpk_nuke_read(&input, 2U, 4U);
        distance = distance_base[index] + xpk_nuke_read(&input, 3U, distance_bits[index]);
        count = index < 4U ? 2U : index < 10U ? 3U : 0U;
        remaining = wanted - produced;
        if (!count) {
            uint32_t code = xpk_nuke_read(&input, 1U, 2U);
            if (!code) {
                count = 6U;
                do {
                    size_t increment;
                    code = xpk_nuke_read(&input, 2U, 4U);
                    increment = code ? 16U - code : 15U;
                    if (input.failed || count > remaining || increment > remaining - count) return false;
                    count += increment;
                } while (!code);
            } else count = 7U - code;
        }
        if (input.failed || !distance || distance > produced || count > remaining) return false;
        while (count--) {
            if ((produced & 4095U) == 0U && xpk_nuke_cancelled(pd)) return false;
            output[produced] = output[produced - distance];
            ++produced;
        }
    }
    if (duke) {
        size_t i;
        uint8_t accumulated = 0U;
        for (i = 0U; i < wanted; ++i) {
            if ((i & 4095U) == 0U && xpk_nuke_cancelled(pd)) return false;
            accumulated = (uint8_t)(accumulated + output[i]);
            output[i] = accumulated;
        }
    }
    return !xpk_nuke_cancelled(pd);
}
#endif
