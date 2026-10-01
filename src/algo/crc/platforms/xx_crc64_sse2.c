/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_crc64_simd_internal.h"

XX_CRC64_TARGET_SSE2
static uint64_t xx_crc64_sse2(uint64_t crc, const void *data, size_t size,
                             bool reflected) {
#ifdef XX_CRC64_X86
    const uint8_t *p = (const uint8_t *)data;
    const __m128i step16 = _mm_set_epi64x((long long)XX_CRC64_K192,
                                         (long long)XX_CRC64_K128);
    const __m128i step64 = _mm_set_epi64x((long long)XX_CRC64_K576,
                                         (long long)XX_CRC64_K512);
    __m128i state;
    uint64_t normal;
    if (!data || size < 16)
        return reflected ? xx_crc64_xz_scalar(crc, data, size)
                         : xx_crc64_ecma_scalar(crc, data, size);
    normal = reflected ? xx_crc64_reverse_bits(~crc) : crc;
    state = xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)p), reflected);
    state = _mm_xor_si128(state, _mm_set_epi64x((long long)normal, 0));
    if (size >= 64) {
        __m128i b = xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)(p + 16)), reflected);
        __m128i c = xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)(p + 32)), reflected);
        __m128i d = xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)(p + 48)), reflected);
        p += 64; size -= 64;
        while (size >= 64) {
            state = _mm_xor_si128(xx_crc64_fold128(state, step64),
                xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)p), reflected));
            b = _mm_xor_si128(xx_crc64_fold128(b, step64),
                xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)(p + 16)), reflected));
            c = _mm_xor_si128(xx_crc64_fold128(c, step64),
                xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)(p + 32)), reflected));
            d = _mm_xor_si128(xx_crc64_fold128(d, step64),
                xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)(p + 48)), reflected));
            p += 64; size -= 64;
        }
        state = _mm_xor_si128(xx_crc64_fold128(state, step16), b);
        state = _mm_xor_si128(xx_crc64_fold128(state, step16), c);
        state = _mm_xor_si128(xx_crc64_fold128(state, step16), d);
    } else {
        p += 16; size -= 16;
    }
    while (size >= 16) {
        state = _mm_xor_si128(xx_crc64_fold128(state, step16),
            xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)p), reflected));
        p += 16; size -= 16;
    }
    crc = xx_crc64_finish128(state, reflected);
    return reflected ? xx_crc64_xz_scalar(crc, p, size)
                     : xx_crc64_ecma_scalar(crc, p, size);
#else
    return reflected ? xx_crc64_xz_scalar(crc, data, size)
                     : xx_crc64_ecma_scalar(crc, data, size);
#endif
}

XX_CRC64_TARGET_SSE2
uint64_t xx_crc64_xz_sse2(uint64_t crc, const void *data, size_t size) {
    return xx_crc64_sse2(crc, data, size, true);
}

XX_CRC64_TARGET_SSE2
uint64_t xx_crc64_ecma_sse2(uint64_t crc, const void *data, size_t size) {
    return xx_crc64_sse2(crc, data, size, false);
}
