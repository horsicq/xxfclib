/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_adler32_platform.h"

#define ADLER_MOD 65521U
#define ADLER_MAX_DEFER 5552U

#if defined(XX_ADLER32_ARM64) && (defined(_MSC_VER) || defined(__ARM_FEATURE_DOTPROD))
#define XX_ADLER32_HAVE_ARM_DOTPROD 1
#include <arm_neon.h>
#endif

uint32_t xx_adler32_arm_dotprod(uint32_t adler, const void *data, size_t size)
{
#ifdef XX_ADLER32_HAVE_ARM_DOTPROD
    static const uint8_t local_weights_data[16] = {16U, 15U, 14U, 13U, 12U, 11U, 10U, 9U, 8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U};
    const uint8_t *p = (const uint8_t *)data;
    uint32_t a = adler & 0xFFFFU;
    uint32_t b = adler >> 16;
    const uint8x16_t ones = vdupq_n_u8(1U);
    const uint8x16_t sixteen = vdupq_n_u8(16U);
    const uint8x16_t local_weights = vld1q_u8(local_weights_data);

    if (!data || size < 64U) return xx_adler32_scalar(adler, data, size);

    while (size >= 64U) {
        size_t run = size < ADLER_MAX_DEFER ? size : ADLER_MAX_DEFER;
        size_t vector_bytes = run & ~(size_t)15U;
        size_t done = 0U;

        /* UDOT has unsigned-byte operands, so cover each run with chunks no
         * larger than 240 bytes. The chunk-relative weights then fit in one
         * byte while producing the same Adler b contribution as n..1. */
        while (done < vector_bytes) {
            size_t remaining = vector_bytes - done;
            uint32_t chunk = (uint32_t)(remaining < 240U ? remaining : 240U);
            uint8x16_t weights = vaddq_u8(local_weights, vdupq_n_u8((uint8_t)(chunk - 16U)));
            uint32x4_t sums = vdupq_n_u32(0U);
            uint32x4_t weighted = vdupq_n_u32(0U);
            uint32_t i;

            for (i = 0U; i < chunk; i += 16U) {
                uint8x16_t bytes = vld1q_u8(p + done + i);
                sums = vdotq_u32(sums, bytes, ones);
                weighted = vdotq_u32(weighted, bytes, weights);
                weights = vsubq_u8(weights, sixteen);
            }

            b += chunk * a + vaddvq_u32(weighted);
            a += vaddvq_u32(sums);
            done += chunk;
        }

        while (done < run) {
            a += p[done++];
            b += a;
        }

        a %= ADLER_MOD;
        b %= ADLER_MOD;
        p += run;
        size -= run;
    }

    return xx_adler32_scalar((b << 16) | a, p, size);
#else
    return xx_adler32_scalar(adler, data, size);
#endif
}
