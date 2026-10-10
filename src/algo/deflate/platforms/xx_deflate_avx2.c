/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_deflate_platform.h"
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_DEFLATE_X86
#include <immintrin.h>
#endif
#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_DEFLATE_X86)
#define XX_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_TARGET_AVX2
#endif

XX_TARGET_AVX2
size_t xx_deflate_match_avx2(const uint8_t *a, const uint8_t *b, size_t maximum)
{
    size_t at = 0;
#ifdef XX_DEFLATE_X86
    for (; maximum - at >= 32U; at += 32U) {
        __m256i first = _mm256_loadu_si256((const __m256i *)(const void *)(a + at));
        __m256i second = _mm256_loadu_si256((const __m256i *)(const void *)(b + at));
        unsigned mismatch = ~(unsigned)_mm256_movemask_epi8(_mm256_cmpeq_epi8(first, second));
        if (mismatch != 0U) {
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
    }
    _mm256_zeroupper();
#endif
    return at + xx_deflate_match_words(a + at, b + at, maximum - at);
}
