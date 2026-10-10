/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * Native 7z Delta, Swap2, and Swap4 decoder filters. Method IDs and Delta's
 * one-byte distance property are specified by ip7z/7zip DOC/Methods.txt and
 * CPP/7zip/Compress/DeltaFilter.cpp. The arithmetic is independently written
 * from the filter definitions; no upstream source is copied.
 */
#ifndef XX_7ZIP_SIMPLE_FILTERS_NATIVE_H
#define XX_7ZIP_SIMPLE_FILTERS_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define XX_7ZIP_SIMPLE_DELTA UINT64_C(0x03)
#define XX_7ZIP_SIMPLE_SWAP2 UINT64_C(0x020302)
#define XX_7ZIP_SIMPLE_SWAP4 UINT64_C(0x020304)

static bool xx_7zip_simple_filter_native(uint64_t method, const uint8_t *properties, size_t properties_size, const uint8_t *input, size_t input_size, uint8_t *output,
                                         size_t output_size, xx_pd_struct *pd)
{
    size_t i;
    unsigned width;
    if ((!input && input_size) || (!output && output_size) || input_size != output_size || xx_pd_is_stopped(pd)) return false;
    if (method == XX_7ZIP_SIMPLE_DELTA) {
        uint8_t state[256];
        unsigned distance;
        if (properties_size != 1U || !properties) return false;
        distance = (unsigned)properties[0] + 1U;
        memset(state, 0, sizeof(state));
        for (i = 0U; i < input_size; ++i) {
            uint8_t value;
            if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
            value = (uint8_t)(input[i] + state[i % distance]);
            output[i] = value;
            state[i % distance] = value;
        }
        return !xx_pd_is_stopped(pd);
    }
    if (method == XX_7ZIP_SIMPLE_SWAP2) width = 2U;
    else if (method == XX_7ZIP_SIMPLE_SWAP4) width = 4U;
    else return false;
    if (properties_size != 0U) return false;
    for (i = 0U; i < input_size;) {
        uint8_t block[4];
        unsigned j;
        size_t left = input_size - i;
        if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
        if (left < width) {
            for (j = 0U; j < left; ++j) block[j] = input[i + j];
            for (j = 0U; j < left; ++j) output[i + j] = block[j];
            i += left;
        } else {
            for (j = 0U; j < width; ++j) block[j] = input[i + j];
            for (j = 0U; j < width; ++j) output[i + j] = block[width - 1U - j];
            i += width;
        }
    }
    return !xx_pd_is_stopped(pd);
}
#endif
