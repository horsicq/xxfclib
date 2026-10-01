/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_adler32_platform.h"

#define ADLER_MOD 65521U
#define ADLER_MAX_DEFER 5552U

XX_ADLER32_TARGET_SSE2
uint32_t xx_adler32_sse2(uint32_t adler, const void *data, size_t size) {
#ifdef XX_ADLER32_X86
    const uint8_t *p = (const uint8_t *)data;
    uint32_t a = adler & 0xFFFFU;
    uint32_t b = adler >> 16;
    const __m128i zero = _mm_setzero_si128();
    const __m128i sixteen = _mm_set1_epi16(16);
    if (!data || size < 64U) return xx_adler32_scalar(adler, data, size);

    while (size >= 64U) {
        /* Each byte contributes to a once and to b by its distance from the
         * end of this run. At most 5552 bytes are deferred, so all scalar
         * accumulators stay within 32 bits, including a noncanonical seed. */
        size_t run = size < ADLER_MAX_DEFER ? size : ADLER_MAX_DEFER;
        uint32_t n = (uint32_t)(run & ~(size_t)15U);
        __m128i weights_lo = _mm_setr_epi16((short)n, (short)(n-1U), (short)(n-2U), (short)(n-3U),
                                             (short)(n-4U), (short)(n-5U), (short)(n-6U), (short)(n-7U));
        __m128i weights_hi = _mm_setr_epi16((short)(n-8U), (short)(n-9U), (short)(n-10U), (short)(n-11U),
                                             (short)(n-12U), (short)(n-13U), (short)(n-14U), (short)(n-15U));
        __m128i sums = zero;
        __m128i weighted = zero;
        uint64_t sum_lanes[2];
        uint32_t weighted_lanes[4];
        uint32_t sum = 0U, weight_sum = 0U;
        uint32_t i;
        for (i = 0U; i < n; i += 16U) {
            __m128i bytes = _mm_loadu_si128((const __m128i *)(const void *)(p + i));
            __m128i lo = _mm_unpacklo_epi8(bytes, zero);
            __m128i hi = _mm_unpackhi_epi8(bytes, zero);
            sums = _mm_add_epi64(sums, _mm_sad_epu8(bytes, zero));
            weighted = _mm_add_epi32(weighted, _mm_madd_epi16(lo, weights_lo));
            weighted = _mm_add_epi32(weighted, _mm_madd_epi16(hi, weights_hi));
            weights_lo = _mm_sub_epi16(weights_lo, sixteen);
            weights_hi = _mm_sub_epi16(weights_hi, sixteen);
        }
        _mm_storeu_si128((__m128i *)(void *)sum_lanes, sums);
        _mm_storeu_si128((__m128i *)(void *)weighted_lanes, weighted);
        sum = (uint32_t)(sum_lanes[0] + sum_lanes[1]);
        for (i = 0U; i < 4U; ++i) weight_sum += weighted_lanes[i];
        b += n * a + weight_sum;
        a += sum;
        a %= ADLER_MOD;
        b %= ADLER_MOD;
        p += n;
        size -= n;
    }
    return xx_adler32_scalar((b << 16) | a, p, size);
#else
    return xx_adler32_scalar(adler, data, size);
#endif
}
