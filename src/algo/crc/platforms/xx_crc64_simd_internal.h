/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_CRC64_SIMD_INTERNAL_H
#define XX_CRC64_SIMD_INTERNAL_H

#include "xx_crc64_platform.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_CRC64_X86 1
#include <immintrin.h>
#endif

#if defined(_MSC_VER)
#define XX_CRC64_INLINE __forceinline
#else
#define XX_CRC64_INLINE inline __attribute__((always_inline))
#endif
#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_CRC64_X86)
#define XX_CRC64_TARGET_SSE2 __attribute__((target("sse2,pclmul")))
#define XX_CRC64_TARGET_AVX2 __attribute__((target("avx2,pclmul")))
#else
#define XX_CRC64_TARGET_SSE2
#define XX_CRC64_TARGET_AVX2
#endif

#ifdef XX_CRC64_X86
/* All arithmetic uses G(x) = x^64 + 0x42F0E1EBA9EA3693. Reflected XZ
 * seeds/bytes are reversed on entry/exit; ECMA-182 uses this order directly.
 * These constants are x^n mod G, obtained by n shift/XOR polynomial steps:
 * x^64, x^128, x^192, x^512, x^576. No writable lookup tables are needed. */
#define XX_CRC64_POLY UINT64_C(0x42F0E1EBA9EA3693)
#define XX_CRC64_K128 UINT64_C(0x05F5C3C7EB52FAB6)
#define XX_CRC64_K192 UINT64_C(0x4EB938A7D257740E)
#define XX_CRC64_K512 UINT64_C(0x5F6843CA540DF020)
#define XX_CRC64_K576 UINT64_C(0xDDF4B6981205B83F)

static XX_CRC64_INLINE uint64_t xx_crc64_reverse_bits(uint64_t v) {
    v = ((v >> 1) & UINT64_C(0x5555555555555555)) |
        ((v & UINT64_C(0x5555555555555555)) << 1);
    v = ((v >> 2) & UINT64_C(0x3333333333333333)) |
        ((v & UINT64_C(0x3333333333333333)) << 2);
    v = ((v >> 4) & UINT64_C(0x0F0F0F0F0F0F0F0F)) |
        ((v & UINT64_C(0x0F0F0F0F0F0F0F0F)) << 4);
    v = ((v >> 8) & UINT64_C(0x00FF00FF00FF00FF)) |
        ((v & UINT64_C(0x00FF00FF00FF00FF)) << 8);
    v = ((v >> 16) & UINT64_C(0x0000FFFF0000FFFF)) |
        ((v & UINT64_C(0x0000FFFF0000FFFF)) << 16);
    return (v >> 32) | (v << 32);
}

XX_CRC64_TARGET_SSE2
static XX_CRC64_INLINE __m128i xx_crc64_order128(__m128i v, bool reflected) {
    if (reflected) {
        const __m128i m1 = _mm_set1_epi8(0x55);
        const __m128i m2 = _mm_set1_epi8(0x33);
        const __m128i m4 = _mm_set1_epi8(0x0f);
        v = _mm_or_si128(_mm_and_si128(_mm_srli_epi16(v, 1), m1),
                        _mm_slli_epi16(_mm_and_si128(v, m1), 1));
        v = _mm_or_si128(_mm_and_si128(_mm_srli_epi16(v, 2), m2),
                        _mm_slli_epi16(_mm_and_si128(v, m2), 2));
        v = _mm_or_si128(_mm_and_si128(_mm_srli_epi16(v, 4), m4),
                        _mm_slli_epi16(_mm_and_si128(v, m4), 4));
    }
    v = _mm_or_si128(_mm_srli_epi16(v, 8), _mm_slli_epi16(v, 8));
    v = _mm_or_si128(_mm_srli_epi32(v, 16), _mm_slli_epi32(v, 16));
    return _mm_shuffle_epi32(v, _MM_SHUFFLE(0, 1, 2, 3));
}

/* If F = high*x^64 + low, multiplying by x^n modulo G can be folded
 * into 128 bits as high*(x^(n+64) mod G) XOR low*(x^n mod G).
 * Keep that unreduced congruent polynomial until the end of the buffer. */
XX_CRC64_TARGET_SSE2
static XX_CRC64_INLINE __m128i xx_crc64_fold128(__m128i state, __m128i powers) {
    return _mm_xor_si128(_mm_clmulepi64_si128(state, powers, 0x00),
                         _mm_clmulepi64_si128(state, powers, 0x11));
}

XX_CRC64_TARGET_SSE2
static XX_CRC64_INLINE uint64_t xx_crc64_finish128(__m128i state, bool reflected) {
    uint64_t halves[2], high, result;
    /* Apply the CRC's final x^64, then reduce the single 128-bit result.
     * Only this final reduction is scalar; all payload folding is CLMUL. */
    state = xx_crc64_fold128(state,
        _mm_set_epi64x((long long)XX_CRC64_K128, (long long)XX_CRC64_POLY));
    _mm_storeu_si128((__m128i *)(void *)halves, state);
    high = halves[1];
    for (unsigned bit = 0; bit < 64; ++bit)
        high = (high << 1) ^ (XX_CRC64_POLY & (UINT64_C(0) - (high >> 63)));
    result = high ^ halves[0];
    return reflected ? ~xx_crc64_reverse_bits(result) : result;
}
#endif
#endif
