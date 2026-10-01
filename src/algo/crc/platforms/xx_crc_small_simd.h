/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_CRC_SMALL_SIMD_H
#define XX_CRC_SMALL_SIMD_H

#include "xx_crc64_simd_internal.h"

/* Powers are x^n modulo x^width + polynomial, in normal bit order. The
 * payload folding is common to normal and reflected CRCs: reflected input
 * and seed are bit-reversed on entry and the remainder on exit. */
typedef struct xx_crc_small_params_s {
    uint32_t polynomial;
    unsigned width;
    bool reflected;
    uint32_t k128;
    uint32_t k192;
    uint32_t k512;
    uint32_t k576;
    uint32_t kw;
    uint32_t kw64;
} xx_crc_small_params;

#ifdef XX_CRC64_X86
static XX_CRC64_INLINE uint32_t xx_crc_small_reverse(uint32_t value, unsigned width) {
    uint32_t result = 0;
    for (unsigned bit = 0; bit < width; ++bit) {
        result = (result << 1) | (value & 1U);
        value >>= 1;
    }
    return result;
}

/* The final PCLMUL product has at most width significant bits in its high
 * half. Reduce only the remaining 64 low bits with the usual polynomial
 * recurrence; the payload itself has already been folded in SIMD registers. */
XX_CRC64_TARGET_SSE2
static XX_CRC64_INLINE uint32_t xx_crc_small_reduce(__m128i state,
                                                     const xx_crc_small_params *params) {
    uint64_t halves[2];
    uint64_t remainder;
    const uint64_t top = UINT64_C(1) << params->width;
    _mm_storeu_si128((__m128i *)(void *)halves, state);
    remainder = halves[1];
    for (unsigned bit = 64; bit != 0; --bit) {
        remainder = (remainder << 1) | ((halves[0] >> (bit - 1)) & 1U);
        if (remainder & top) remainder ^= top | params->polynomial;
    }
    return (uint32_t)remainder;
}

/* The input size must be a positive multiple of 16 bytes. This helper needs
 * SSE2 and PCLMULQDQ; the caller checks CPU support and the shared SSE2
 * switch before dispatching. Four independent lanes hide CLMUL latency. */
XX_CRC64_TARGET_SSE2
static uint32_t xx_crc_small_pclmul(uint32_t crc, const uint8_t *data,
                                    size_t size, const xx_crc_small_params *params) {
    const __m128i step16 = _mm_set_epi64x((long long)params->k192,
                                         (long long)params->k128);
    const __m128i step64 = _mm_set_epi64x((long long)params->k576,
                                         (long long)params->k512);
    const __m128i finish = _mm_set_epi64x((long long)params->kw64,
                                         (long long)params->kw);
    const uint8_t *p = data;
    uint32_t normal = params->reflected ?
        xx_crc_small_reverse(crc, params->width) : crc;
    __m128i state = xx_crc64_order128(
        _mm_loadu_si128((const __m128i *)(const void *)p), params->reflected);
    state = _mm_xor_si128(state,
        _mm_set_epi64x((long long)((uint64_t)normal << (64 - params->width)), 0));
    if (size >= 64) {
        __m128i b = xx_crc64_order128(
            _mm_loadu_si128((const __m128i *)(const void *)(p + 16)), params->reflected);
        __m128i c = xx_crc64_order128(
            _mm_loadu_si128((const __m128i *)(const void *)(p + 32)), params->reflected);
        __m128i d = xx_crc64_order128(
            _mm_loadu_si128((const __m128i *)(const void *)(p + 48)), params->reflected);
        p += 64;
        size -= 64;
        while (size >= 64) {
            state = _mm_xor_si128(xx_crc64_fold128(state, step64),
                xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)p), params->reflected));
            b = _mm_xor_si128(xx_crc64_fold128(b, step64),
                xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)(p + 16)), params->reflected));
            c = _mm_xor_si128(xx_crc64_fold128(c, step64),
                xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)(p + 32)), params->reflected));
            d = _mm_xor_si128(xx_crc64_fold128(d, step64),
                xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)(p + 48)), params->reflected));
            p += 64;
            size -= 64;
        }
        state = _mm_xor_si128(xx_crc64_fold128(state, step16), b);
        state = _mm_xor_si128(xx_crc64_fold128(state, step16), c);
        state = _mm_xor_si128(xx_crc64_fold128(state, step16), d);
    } else {
        p += 16;
        size -= 16;
    }
    while (size >= 16) {
        state = _mm_xor_si128(xx_crc64_fold128(state, step16),
            xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)p), params->reflected));
        p += 16;
        size -= 16;
    }
    normal = xx_crc_small_reduce(xx_crc64_fold128(state, finish), params);
    return params->reflected ? xx_crc_small_reverse(normal, params->width) : normal;
}
#endif /* XX_CRC64_X86 */
#endif /* XX_CRC_SMALL_SIMD_H */
