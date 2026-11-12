/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Port of XArchive/Algos/xpanoramadecoder.cpp.
 */
#include "xxfclib/algo/panorama/xx_panorama.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/global/xx_global.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_PANORAMA_X86 1
#include <immintrin.h>
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_PANORAMA_X86)
#define XX_PANORAMA_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_PANORAMA_TARGET_AVX2
#endif

#define PANORAMA_MULTIPLIER 0x8088405U
#define PANORAMA_RAR_SIGNATURE_LOW 0x21726152U /* "Rar!"      */
#define PANORAMA_RAR_SIGNATURE_HIGH 0x71AU     /* 1a 07 00    */
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

#ifdef XX_PANORAMA_X86
XX_PANORAMA_TARGET_AVX2
static void panorama_xor_avx2(const uint8_t *input, uint8_t *output, size_t input_size, const uint8_t *keystream)
{
    __m256i k[32];
    size_t full_blocks = input_size / PANORAMA_KEYSTREAM_SIZE;
    size_t offset = 0U;
    size_t b;
    unsigned j;

    for (j = 0U; j < 32U; ++j) {
        k[j] = _mm256_loadu_si256((const __m256i *)(const void *)(keystream + j * 32U));
    }
    for (b = 0U; b < full_blocks; ++b) {
        for (j = 0U; j < 32U; ++j) {
            __m256i in_vec = _mm256_loadu_si256((const __m256i *)(const void *)(input + offset + j * 32U));
            __m256i out_vec = _mm256_xor_si256(in_vec, k[j]);
            _mm256_storeu_si256((__m256i *)(void *)(output + offset + j * 32U), out_vec);
        }
        offset += PANORAMA_KEYSTREAM_SIZE;
    }
    for (; offset + 32U <= input_size; offset += 32U) {
        __m256i in_vec = _mm256_loadu_si256((const __m256i *)(const void *)(input + offset));
        __m256i key_vec = _mm256_loadu_si256((const __m256i *)(const void *)(keystream + (offset & (PANORAMA_KEYSTREAM_SIZE - 1U))));
        _mm256_storeu_si256((__m256i *)(void *)(output + offset), _mm256_xor_si256(in_vec, key_vec));
    }
    for (; offset + 16U <= input_size; offset += 16U) {
        __m128i in_vec = _mm_loadu_si128((const __m128i *)(const void *)(input + offset));
        __m128i key_vec = _mm_loadu_si128((const __m128i *)(const void *)(keystream + (offset & (PANORAMA_KEYSTREAM_SIZE - 1U))));
        _mm_storeu_si128((__m128i *)(void *)(output + offset), _mm_xor_si128(in_vec, key_vec));
    }
    for (; offset < input_size; ++offset) {
        output[offset] = (uint8_t)(input[offset] ^ keystream[offset & (PANORAMA_KEYSTREAM_SIZE - 1U)]);
    }
    _mm256_zeroupper();
}

static void panorama_xor_sse2(const uint8_t *input, uint8_t *output, size_t input_size, const uint8_t *keystream)
{
    size_t offset = 0U;
    for (; offset + 16U <= input_size; offset += 16U) {
        __m128i in_vec = _mm_loadu_si128((const __m128i *)(const void *)(input + offset));
        __m128i key_vec = _mm_loadu_si128((const __m128i *)(const void *)(keystream + (offset & (PANORAMA_KEYSTREAM_SIZE - 1U))));
        _mm_storeu_si128((__m128i *)(void *)(output + offset), _mm_xor_si128(in_vec, key_vec));
    }
    for (; offset < input_size; ++offset) {
        output[offset] = (uint8_t)(input[offset] ^ keystream[offset & (PANORAMA_KEYSTREAM_SIZE - 1U)]);
    }
}
#endif

bool xx_panorama_seed_from_header(const uint8_t *header, size_t header_size, uint32_t *seed)
{
    uint32_t low, high, candidate;

    if (!header || !seed || (header_size < 8U)) return false;

    low = xx_data_get_u32(header, 4, 0, false);
    high = xx_data_get_u32(header + 4, 4, 0, false);
    candidate = low ^ PANORAMA_RAR_SIGNATURE_LOW;
    /* Deliberate, and matches the reference: a zero seed is refused even
     * though it is arithmetically valid, because it would mean the file is
     * plain "Rar!" with no cipher applied. */
    if (candidate == 0U) return false;
    if (((high ^ (candidate * PANORAMA_MULTIPLIER)) & 0xFFFFFFU) != PANORAMA_RAR_SIGNATURE_HIGH) {
        return false;
    }
    *seed = candidate;

    return true;
}

bool xx_panorama_decode_memory_seed(const uint8_t *input, size_t input_size, uint32_t seed, uint8_t *output, size_t output_size, size_t *written)
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

#ifdef XX_PANORAMA_X86
    if (xx_is_avx2_enabled()) {
        panorama_xor_avx2(input, output, input_size, keystream);
    } else if (xx_is_sse2_enabled()) {
        panorama_xor_sse2(input, output, input_size, keystream);
    } else
#endif
    {
        for (i = 0U; i < input_size; ++i) {
            output[i] = (uint8_t)(input[i] ^ keystream[i & (PANORAMA_KEYSTREAM_SIZE - 1U)]);
        }
    }

    if (written) *written = input_size;

    return true;
}

bool xx_panorama_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written)
{
    uint32_t seed = 0U;

    if (written) *written = 0U;
    if (!input || !output) return false;
    if (!xx_panorama_seed_from_header(input, input_size, &seed)) return false;

    return xx_panorama_decode_memory_seed(input, input_size, seed, output, output_size, written);
}

bool xx_panorama_scan_memory(const uint8_t *input, size_t input_size, size_t max_output, size_t *consumed, size_t *produced)
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
