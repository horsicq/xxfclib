/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_adler32.c
 * @brief Adler-32, RFC 1950 section 8.2.
 */

#include "xxfclib/algo/adler32/xx_adler32.h"

/* The largest number of bytes that can be accumulated before `b` could
 * overflow 32 bits. RFC 1950 gives 5552; deferring the modulo that far is
 * what makes this fast, and going further is what makes it wrong. */
#define ADLER_MOD 65521U
#define ADLER_MAX_DEFER 5552U

uint32_t xx_adler32_update(uint32_t adler, const void *data, size_t size) {
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

uint32_t xx_adler32(const void *data, size_t size) {
    return xx_adler32_update(XX_ADLER32_INIT, data, size);
}
