/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_lzma_platform.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_LZMA_X86
#include <emmintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_LZMA_X86)
#define XX_TARGET_SSE2 __attribute__((target("sse2")))
#else
#define XX_TARGET_SSE2
#endif

XX_TARGET_SSE2
void xx_lzma_fill_probs_sse2(uint16_t *probabilities, size_t count)
{
#ifdef XX_LZMA_X86
    const __m128i value = _mm_set1_epi16(1024);
    size_t at = 0;
    for (; count - at >= 8; at += 8) _mm_storeu_si128((__m128i *)(void *)(probabilities + at), value);
    for (; at < count; ++at) probabilities[at] = 1024;
#else
    xx_lzma_fill_probs_scalar(probabilities, count);
#endif
}

XX_TARGET_SSE2
size_t xx_lzma_match_length_sse2(const uint8_t *first, const uint8_t *second, size_t maximum)
{
#ifdef XX_LZMA_X86
    size_t at = 0;
    for (; maximum - at >= 16; at += 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(first + at));
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(second + at));
        unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(a, b));
        if (mask != 0xffffU) {
            unsigned mismatch = (~mask) & 0xffffU;
#ifdef _MSC_VER
            unsigned long bit;
            _BitScanForward(&bit, mismatch);
            return at + bit;
#else
            return at + (unsigned)__builtin_ctz(mismatch);
#endif
        }
    }
    while (at < maximum && first[at] == second[at]) ++at;
    return at;
#else
    return xx_lzma_match_length_scalar(first, second, maximum);
#endif
}
