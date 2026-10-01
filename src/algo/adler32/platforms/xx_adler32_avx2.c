/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_adler32_platform.h"

#define ADLER_MOD 65521U
#define ADLER_MAX_DEFER 5552U

XX_ADLER32_TARGET_AVX2
uint32_t xx_adler32_avx2(uint32_t adler, const void *data, size_t size) {
#ifdef XX_ADLER32_X86
    const uint8_t *p = (const uint8_t *)data;
    uint32_t a = adler & 0xFFFFU;
    uint32_t b = adler >> 16;
    const __m256i zero = _mm256_setzero_si256();
    const __m256i thirty_two = _mm256_set1_epi16(32);
    if (!data || size < 64U) return xx_adler32_scalar(adler, data, size);

    while (size >= 64U) {
        size_t run = size < ADLER_MAX_DEFER ? size : ADLER_MAX_DEFER;
        uint32_t n = (uint32_t)(run & ~(size_t)31U);
        __m256i weights_lo = _mm256_setr_epi16(
            (short)n, (short)(n-1U), (short)(n-2U), (short)(n-3U),
            (short)(n-4U), (short)(n-5U), (short)(n-6U), (short)(n-7U),
            (short)(n-8U), (short)(n-9U), (short)(n-10U), (short)(n-11U),
            (short)(n-12U), (short)(n-13U), (short)(n-14U), (short)(n-15U));
        __m256i weights_hi = _mm256_sub_epi16(weights_lo, _mm256_set1_epi16(16));
        __m256i sums = zero;
        __m256i weighted = zero;
        uint64_t sum_lanes[4];
        uint32_t weighted_lanes[8];
        uint32_t sum = 0U, weight_sum = 0U;
        uint32_t i;
        for (i = 0U; i < n; i += 32U) {
            __m256i bytes = _mm256_loadu_si256((const __m256i *)(const void *)(p + i));
            __m128i lo_bytes = _mm256_castsi256_si128(bytes);
            __m128i hi_bytes = _mm256_extracti128_si256(bytes, 1);
            __m256i lo = _mm256_cvtepu8_epi16(lo_bytes);
            __m256i hi = _mm256_cvtepu8_epi16(hi_bytes);
            sums = _mm256_add_epi64(sums, _mm256_sad_epu8(bytes, zero));
            weighted = _mm256_add_epi32(weighted, _mm256_madd_epi16(lo, weights_lo));
            weighted = _mm256_add_epi32(weighted, _mm256_madd_epi16(hi, weights_hi));
            weights_lo = _mm256_sub_epi16(weights_lo, thirty_two);
            weights_hi = _mm256_sub_epi16(weights_hi, thirty_two);
        }
        _mm256_storeu_si256((__m256i *)(void *)sum_lanes, sums);
        _mm256_storeu_si256((__m256i *)(void *)weighted_lanes, weighted);
        for (i = 0U; i < 4U; ++i) sum += (uint32_t)sum_lanes[i];
        for (i = 0U; i < 8U; ++i) weight_sum += weighted_lanes[i];
        b += n * a + weight_sum;
        a += sum;
        a %= ADLER_MOD;
        b %= ADLER_MOD;
        p += n;
        size -= n;
    }
    _mm256_zeroupper();
    return xx_adler32_scalar((b << 16) | a, p, size);
#else
    return xx_adler32_scalar(adler, data, size);
#endif
}
