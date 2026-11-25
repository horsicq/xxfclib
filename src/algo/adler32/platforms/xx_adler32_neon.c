/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_adler32_platform.h"

#ifdef XX_ADLER32_ARM64
#include <arm_neon.h>
#endif

#define ADLER_MOD 65521U
#define ADLER_MAX_DEFER 5552U

uint32_t xx_adler32_neon(uint32_t adler, const void *data, size_t size)
{
#ifdef XX_ADLER32_ARM64
    static const uint8_t weights_data[16] = {16U, 15U, 14U, 13U, 12U, 11U, 10U, 9U, 8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U};
    const uint8_t *p = (const uint8_t *)data;
    uint32_t a = adler & 0xFFFFU;
    uint32_t b = adler >> 16;
    uint8x16_t weights;

    if (!p || size < 64U) return xx_adler32_scalar(adler, data, size);
    weights = vld1q_u8(weights_data);
    while (size >= 64U) {
        size_t run = size < ADLER_MAX_DEFER ? size : ADLER_MAX_DEFER;
        size_t done = 0U;
        for (; run - done >= 16U; done += 16U) {
            uint8x16_t bytes = vld1q_u8(p + done);
            uint16x8_t low = vmull_u8(vget_low_u8(bytes), vget_low_u8(weights));
            uint16x8_t high = vmull_u8(vget_high_u8(bytes), vget_high_u8(weights));
            uint32_t sum = (uint32_t)vaddlvq_u8(bytes);
            uint32_t weighted = vaddlvq_u16(low) + vaddlvq_u16(high);
            b += 16U * a + weighted;
            a += sum;
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
