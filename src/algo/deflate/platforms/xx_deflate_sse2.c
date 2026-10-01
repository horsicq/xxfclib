/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_deflate_platform.h"
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_DEFLATE_X86
#include <emmintrin.h>
#endif
#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_DEFLATE_X86)
#define XX_TARGET_SSE2 __attribute__((target("sse2")))
#else
#define XX_TARGET_SSE2
#endif

XX_TARGET_SSE2
size_t xx_deflate_match_sse2(const uint8_t *a, const uint8_t *b, size_t maximum) {
    size_t at = 0;
#ifdef XX_DEFLATE_X86
    for (; maximum - at >= 16U; at += 16U) {
        __m128i first = _mm_loadu_si128((const __m128i *)(const void *)(a + at));
        __m128i second = _mm_loadu_si128((const __m128i *)(const void *)(b + at));
        unsigned mismatch = (~(unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(first, second))) & 0xffffU;
        if (mismatch != 0U) {
#ifdef _MSC_VER
            unsigned long bit;
            _BitScanForward(&bit, mismatch);
            return at + bit;
#else
            return at + (unsigned)__builtin_ctz(mismatch);
#endif
        }
    }
#endif
    return at + xx_deflate_match_words(a + at, b + at, maximum - at);
}
