/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_lzma_platform.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_LZMA_X86
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_LZMA_X86)
#define XX_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_TARGET_AVX2
#endif

XX_TARGET_AVX2
void xx_lzma_fill_probs_avx2(uint16_t *probabilities, size_t count)
{
#ifdef XX_LZMA_X86
    const __m256i value = _mm256_set1_epi16(1024);
    size_t at = 0;
    for (; count - at >= 16; at += 16) _mm256_storeu_si256((__m256i *)(void *)(probabilities + at), value);
    if (count - at >= 8) {
        _mm_storeu_si128((__m128i *)(void *)(probabilities + at), _mm_set1_epi16(1024));
        at += 8;
    }
    for (; at < count; ++at) probabilities[at] = 1024;
    _mm256_zeroupper();
#else
    xx_lzma_fill_probs_scalar(probabilities, count);
#endif
}

XX_TARGET_AVX2
size_t xx_lzma_match_length_avx2(const uint8_t *first, const uint8_t *second, size_t maximum)
{
#ifdef XX_LZMA_X86
    size_t at = 0;
    for (; maximum - at >= 32; at += 32) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(first + at));
        __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(second + at));
        unsigned mask = (unsigned)_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, b));
        if (mask != UINT32_MAX) {
            unsigned bit;
#ifdef _MSC_VER
            unsigned long first_bit;
            _BitScanForward(&first_bit, ~mask);
            bit = (unsigned)first_bit;
#else
            bit = (unsigned)__builtin_ctz(~mask);
#endif
            _mm256_zeroupper();
            return at + bit;
        }
    }
    if (maximum - at >= 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(first + at));
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(second + at));
        unsigned mismatch = (~(unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(a, b))) & 0xffffU;
        if (mismatch != 0) {
            unsigned bit;
#ifdef _MSC_VER
            unsigned long first_bit;
            _BitScanForward(&first_bit, mismatch);
            bit = (unsigned)first_bit;
#else
            bit = (unsigned)__builtin_ctz(mismatch);
#endif
            _mm256_zeroupper();
            return at + bit;
        }
        at += 16;
    }
    while (at < maximum && first[at] == second[at]) ++at;
    _mm256_zeroupper();
    return at;
#else
    return xx_lzma_match_length_scalar(first, second, maximum);
#endif
}
