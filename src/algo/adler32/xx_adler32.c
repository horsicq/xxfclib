/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_adler32.c
 * @brief Adler-32, RFC 1950 section 8.2.
 */

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/global/xx_global.h"
#include "platforms/xx_adler32_platform.h"

/* The largest number of bytes that can be accumulated before `b` could
 * overflow 32 bits. RFC 1950 gives 5552; deferring the modulo that far is
 * what makes this fast, and going further is what makes it wrong. */
#define ADLER_MOD 65521U
#define ADLER_MAX_DEFER 5552U

uint32_t xx_adler32_scalar(uint32_t adler, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t a = adler & 0xFFFFU;
    uint32_t b = (adler >> 16) & 0xFFFFU;

    if (!data) return adler;

    while (size > 0U) {
        size_t run = (size < ADLER_MAX_DEFER) ? size : ADLER_MAX_DEFER;
        size -= run;
        while (run-- > 0U) {
            a += *bytes++;
            b += a;
        }
        a %= ADLER_MOD;
        b %= ADLER_MOD;
    }

    return (b << 16) | a;
}

uint32_t xx_adler32_update(uint32_t adler, const void *data, size_t size)
{
    if (!data || size < 64U) return xx_adler32_scalar(adler, data, size);
#ifdef XX_ADLER32_X86
    /* As with CRC64, consult the switches on every call so applications can
     * disable a backend at runtime. Both SIMD paths also handle scalar tails. */
    bool sse2_enabled = xx_is_sse2_enabled();
    if (xx_is_avx2_enabled() && (!sse2_enabled || size >= 1024U * 1024U)) return xx_adler32_avx2(adler, data, size);
    if (sse2_enabled) return xx_adler32_sse2(adler, data, size);
#endif
    return xx_adler32_scalar(adler, data, size);
}

uint32_t xx_adler32(const void *data, size_t size)
{
    return xx_adler32_update(XX_ADLER32_INIT, data, size);
}
