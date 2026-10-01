/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 *
 * Bounded native C adaptation of Ancient ZENODecompressor.cpp, revision
 * 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_ZENO_NATIVE_H
#define XX_XPK_ZENO_NATIVE_H
#include "xx_xpk_lin_native.h"
#include "xxfclib/memory/xx_memory.h"

static bool xpk_zeno_native(const uint8_t *packed, size_t size,
                            uint8_t *output, size_t wanted, xx_pd_struct *pd) {
    xpk_lin_stream stream;
    uint32_t *prefix = NULL;
    uint8_t *suffix = NULL, *stack = NULL;
    uint32_t capacity, free_code, previous, first, bits;
    size_t position = 0;
    bool valid = false;
    unsigned max_bits;
    if (!packed || !output || size < 7U || !wanted ||
        packed[0] || packed[1] || packed[2] || packed[3] ||
        xx_pd_is_stopped(pd)) return false;
    max_bits = packed[4];
    if (max_bits < 9U || max_bits > 20U ||
        (size_t)packed[5] + 6U >= size) return false;
    capacity = UINT32_C(1) << max_bits;
    prefix = (uint32_t *)xx_mem_alloc((size_t)capacity * sizeof(*prefix));
    suffix = (uint8_t *)xx_mem_alloc(capacity);
    stack = (uint8_t *)xx_mem_alloc(5000U);
    if (!prefix || !suffix || !stack) goto done;
    stream.data = packed; stream.front = (size_t)packed[5] + 6U; stream.back = size;
    stream.cached = 0U; stream.available = 0U;
    free_code = 259U; bits = 9U;
    if (!xpk_lin_bits(&stream, bits, &previous) || previous >= 256U) goto done;
    first = previous;
    prefix[258] = 0U; suffix[258] = 0U;
    output[position++] = (uint8_t)first;
    while (position < wanted) {
        uint32_t code, cursor;
        size_t length = 0U, index;
        bool special;
        if (xx_pd_is_stopped(pd)) goto done;
        if (free_code + 3U >= (UINT32_C(1) << bits) && bits < max_bits) ++bits;
        if (!xpk_lin_bits(&stream, bits, &code) || code == 256U) goto done;
        if (code == 257U) {
            bits = 9U; free_code = 258U;
            /* This producer resets width/dictionary, but retains prev/first. */
            continue;
        }
        if (code > free_code || code >= capacity) goto done;
        special = code == free_code;
        cursor = special ? previous : code;
        if (special) stack[length++] = (uint8_t)first;
        while (cursor >= 258U) {
            if (cursor >= free_code || length + 1U >= 5000U) goto done;
            stack[length++] = suffix[cursor];
            cursor = prefix[cursor];
            if ((length & 255U) == 0U && xx_pd_is_stopped(pd)) goto done;
        }
        if (cursor >= 256U || length >= 5000U) goto done;
        first = cursor;
        stack[length++] = (uint8_t)first;
        if (length > wanted - position) goto done;
        for (index = length; index != 0U; --index) {
            if (((length - index) & 1023U) == 0U && xx_pd_is_stopped(pd)) goto done;
            output[position++] = stack[index - 1U];
        }
        if (free_code < capacity) {
            prefix[free_code] = previous;
            suffix[free_code] = (uint8_t)first;
            ++free_code;
        }
        previous = code;
    }
    valid = !xx_pd_is_stopped(pd);
done:
    xx_mem_free(stack); xx_mem_free(suffix); xx_mem_free(prefix);
    return valid;
}
#endif
