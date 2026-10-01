/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Bounded C adaptation of StormLib src/adpcm/adpcm.cpp (Ladislav Zezula,
 * based on Tom Amigo's released implementation), commit 44ebfbf. The
 * original MIT license grant is retained in xx_mpq_huffman_tables.h.
 */
#ifndef XX_MPQ_ADPCM_NATIVE_H
#define XX_MPQ_ADPCM_NATIVE_H

#include "xxfclib/data/xx_pd.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const int8_t mpq_adpcm_next_step[32] = {
    -1, 0, -1, 4, -1, 2, -1, 6,
    -1, 1, -1, 5, -1, 3, -1, 7,
    -1, 1, -1, 5, -1, 3, -1, 7,
    -1, 2, -1, 4, -1, 6, -1, 8
};
static const int16_t mpq_adpcm_step_size[89] = {
       7,     8,     9,    10,    11,    12,    13,    14,
      16,    17,    19,    21,    23,    25,    28,    31,
      34,    37,    41,    45,    50,    55,    60,    66,
      73,    80,    88,    97,   107,   118,   130,   143,
     157,   173,   190,   209,   230,   253,   279,   307,
     337,   371,   408,   449,   494,   544,   598,   658,
     724,   796,   876,   963,  1060,  1166,  1282,  1411,
    1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
   15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
   32767
};

static bool mpq_adpcm_emit(int sample, uint8_t *output,
                           size_t capacity, size_t *written) {
    uint16_t value;
    if (*written > capacity || capacity - *written < 2U) return false;
    value = (uint16_t)sample;
    output[(*written)++] = (uint8_t)value;
    output[(*written)++] = (uint8_t)(value >> 8U);
    return true;
}
static bool mpq_adpcm_decode(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             unsigned channels, xx_pd_struct *pd) {
    int predicted[2] = {0, 0}, step_index[2] = {0x2c, 0x2c};
    unsigned shift, channel;
    size_t at = 2U, written = 0U;
    if (!input || !output || (channels != 1U && channels != 2U) ||
        input_size < 2U + channels * 2U || output_size < channels * 2U ||
        input[0] != 0U || input[1] > 15U ||
        (pd && xx_pd_is_stopped(pd))) return false;
    shift = input[1];
    for (channel = 0U; channel < channels; ++channel) {
        unsigned raw = (unsigned)input[at] | ((unsigned)input[at + 1U] << 8U);
        at += 2U;
        predicted[channel] = raw >= 0x8000U ? (int)raw - 65536 : (int)raw;
        if (!mpq_adpcm_emit(predicted[channel], output, output_size,
                            &written)) return false;
    }
    channel = channels - 1U;
    while (at < input_size) {
        unsigned token = input[at++];
        int size, difference, index;
        if ((at & 255U) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        channel = (channel + 1U) % channels;
        if (token == 0x80U) {
            if (step_index[channel] != 0) --step_index[channel];
            if (!mpq_adpcm_emit(predicted[channel], output, output_size,
                                &written)) return false;
            continue;
        }
        if (token == 0x81U) {
            step_index[channel] += 8;
            if (step_index[channel] > 88) step_index[channel] = 88;
            channel = (channel + 1U) % channels;
            continue;
        }
        index = step_index[channel];
        size = mpq_adpcm_step_size[index];
        difference = size >> shift;
        if (token & 0x01U) difference += size;
        if (token & 0x02U) difference += size >> 1U;
        if (token & 0x04U) difference += size >> 2U;
        if (token & 0x08U) difference += size >> 3U;
        if (token & 0x10U) difference += size >> 4U;
        if (token & 0x20U) difference += size >> 5U;
        predicted[channel] += (token & 0x40U) ? -difference : difference;
        if (predicted[channel] < -32768) predicted[channel] = -32768;
        if (predicted[channel] > 32767) predicted[channel] = 32767;
        if (!mpq_adpcm_emit(predicted[channel], output, output_size,
                            &written)) return false;
        step_index[channel] += mpq_adpcm_next_step[token & 0x1fU];
        if (step_index[channel] < 0) step_index[channel] = 0;
        if (step_index[channel] > 88) step_index[channel] = 88;
    }
    return written == output_size && (!pd || !xx_pd_is_stopped(pd));
}

#endif /* XX_MPQ_ADPCM_NATIVE_H */
