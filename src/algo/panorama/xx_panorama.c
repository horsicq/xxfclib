/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Port of XArchive/Algos/xpanoramadecoder.cpp.
 */
#include "xxfclib/algo/panorama/xx_panorama.h"

#define PANORAMA_MULTIPLIER 0x8088405U
#define PANORAMA_RAR_SIGNATURE_LOW 0x21726152U  /* "Rar!"      */
#define PANORAMA_RAR_SIGNATURE_HIGH 0x71AU      /* 1a 07 00    */
#define PANORAMA_KEYSTREAM_SIZE 1024U

/* The pad is 256 little-endian words of the LCG.  It depends on the stream, so
 * it lives in a caller-owned local - never a file-scope array. */
static void panorama_build_keystream(uint32_t seed, uint8_t *keystream)
{
    uint32_t value = seed;
    unsigned i;

    for (i = 0U; i < 256U; ++i) {
        keystream[i * 4U + 0U] = (uint8_t)(value & 0xFFU);
        keystream[i * 4U + 1U] = (uint8_t)((value >> 8) & 0xFFU);
        keystream[i * 4U + 2U] = (uint8_t)((value >> 16) & 0xFFU);
        keystream[i * 4U + 3U] = (uint8_t)((value >> 24) & 0xFFU);
        value = value * PANORAMA_MULTIPLIER;
    }
}

static uint32_t panorama_read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool xx_panorama_seed_from_header(const uint8_t *header, size_t header_size,
                                  uint32_t *seed)
{
    uint32_t low, high, candidate;

    if (!header || !seed || (header_size < 8U)) return false;

    low = panorama_read_u32le(header);
    high = panorama_read_u32le(header + 4);
    candidate = low ^ PANORAMA_RAR_SIGNATURE_LOW;
    /* Deliberate, and matches the reference: a zero seed is refused even
     * though it is arithmetically valid, because it would mean the file is
     * plain "Rar!" with no cipher applied. */
    if (candidate == 0U) return false;
    if (((high ^ (candidate * PANORAMA_MULTIPLIER)) & 0xFFFFFFU) !=
        PANORAMA_RAR_SIGNATURE_HIGH) {
        return false;
    }
    *seed = candidate;

    return true;
}

bool xx_panorama_decode_memory_seed(const uint8_t *input, size_t input_size,
                                    uint32_t seed, uint8_t *output,
                                    size_t output_size, size_t *written)
{
    uint8_t keystream[PANORAMA_KEYSTREAM_SIZE];
    size_t i;

    if (written) *written = 0U;
    if (!input || !output) return false;
    /* The reference refuses an empty stream outright. */
    if (input_size == 0U) return false;
    /* Running out of capacity is a failure, never a truncation. */
    if (output_size < input_size) return false;

    panorama_build_keystream(seed, keystream);

    for (i = 0U; i < input_size; ++i) {
        output[i] = (uint8_t)(input[i] ^
                              keystream[i & (PANORAMA_KEYSTREAM_SIZE - 1U)]);
    }

    if (written) *written = input_size;

    return true;
}

bool xx_panorama_decode_memory(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               size_t *written)
{
    uint32_t seed = 0U;

    if (written) *written = 0U;
    if (!input || !output) return false;
    if (!xx_panorama_seed_from_header(input, input_size, &seed)) return false;

    return xx_panorama_decode_memory_seed(input, input_size, seed, output,
                                          output_size, written);
}

bool xx_panorama_scan_memory(const uint8_t *input, size_t input_size,
                             size_t max_output, size_t *consumed,
                             size_t *produced)
{
    uint32_t seed = 0U;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!input) return false;
    if (!xx_panorama_seed_from_header(input, input_size, &seed)) return false;
    if (input_size > max_output) return false;

    if (consumed) *consumed = input_size;
    if (produced) *produced = input_size;

    return true;
}
