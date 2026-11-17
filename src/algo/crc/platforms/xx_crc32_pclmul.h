/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_CRC32_PCLMUL_H
#define XX_CRC32_PCLMUL_H

#include "xx_crc64_simd_internal.h"

#ifdef XX_CRC64_X86
/* CRC-32/ISO-HDLC (reflected 0x04C11DB7) folded directly in the reflected
 * domain, so no per-block bit reversal is needed. Constants are bit-reflected
 * x^n mod P(x), shifted left by one (Intel, "Fast CRC Computation for Generic
 * Polynomials Using PCLMULQDQ"): the low qword of a 128-bit lane holds the
 * higher-degree half and is multiplied by x^(n+32), the high qword by
 * x^(n-32), for a fold distance of n bits.
 *   fold 512 (four lanes): x^544, x^480    fold 128: x^160, x^96
 *   64 -> 32: x^64     Barrett: P' = P(x) reflected, mu' = floor(x^64 / P) reflected */
#define XX_CRC32_ISO_K544 0x154442BD4LL
#define XX_CRC32_ISO_K480 0x1C6E41596LL
#define XX_CRC32_ISO_K160 0x1751997D0LL
#define XX_CRC32_ISO_K96 0x0CCAA009ELL
#define XX_CRC32_ISO_K64 0x163CD6124LL
#define XX_CRC32_ISO_P 0x1DB710641LL
#define XX_CRC32_ISO_MU 0x1F7011641LL

XX_CRC64_TARGET_SSE2
static XX_CRC64_INLINE __m128i xx_crc32_iso_fold16(__m128i state, __m128i constants, __m128i data)
{
    __m128i low = _mm_clmulepi64_si128(state, constants, 0x00);
    __m128i high = _mm_clmulepi64_si128(state, constants, 0x11);
    return _mm_xor_si128(_mm_xor_si128(low, high), data);
}

/* reg is the CRC register (the inverted public value); size is a positive
 * multiple of 16. Four independent 128-bit lanes are folded 64 bytes at a
 * time, then merged; a Barrett step reduces the final 64 bits. The result
 * is the register after the data, exactly as the bytewise recurrence. */
XX_CRC64_TARGET_SSE2
static uint32_t xx_crc32_iso_pclmul(uint32_t reg, const uint8_t *p, size_t size)
{
    const __m128i k128 = _mm_set_epi64x(XX_CRC32_ISO_K96, XX_CRC32_ISO_K160);
    const __m128i mask32 = _mm_setr_epi32(-1, 0, -1, 0);
    __m128i x1 = _mm_xor_si128(_mm_loadu_si128((const __m128i *)(const void *)p), _mm_cvtsi32_si128((int)reg));
    __m128i x2;
    if (size >= 64U) {
        const __m128i k512 = _mm_set_epi64x(XX_CRC32_ISO_K480, XX_CRC32_ISO_K544);
        __m128i x3, x4;
        x2 = _mm_loadu_si128((const __m128i *)(const void *)(p + 16));
        x3 = _mm_loadu_si128((const __m128i *)(const void *)(p + 32));
        x4 = _mm_loadu_si128((const __m128i *)(const void *)(p + 48));
        p += 64;
        size -= 64U;
        while (size >= 64U) {
            x1 = xx_crc32_iso_fold16(x1, k512, _mm_loadu_si128((const __m128i *)(const void *)p));
            x2 = xx_crc32_iso_fold16(x2, k512, _mm_loadu_si128((const __m128i *)(const void *)(p + 16)));
            x3 = xx_crc32_iso_fold16(x3, k512, _mm_loadu_si128((const __m128i *)(const void *)(p + 32)));
            x4 = xx_crc32_iso_fold16(x4, k512, _mm_loadu_si128((const __m128i *)(const void *)(p + 48)));
            p += 64;
            size -= 64U;
        }
        x1 = xx_crc32_iso_fold16(x1, k128, x2);
        x1 = xx_crc32_iso_fold16(x1, k128, x3);
        x1 = xx_crc32_iso_fold16(x1, k128, x4);
    } else {
        p += 16;
        size -= 16U;
    }
    while (size >= 16U) {
        x1 = xx_crc32_iso_fold16(x1, k128, _mm_loadu_si128((const __m128i *)(const void *)p));
        p += 16;
        size -= 16U;
    }
    /* 128 -> 64 bits: low qword times x^96, xor the high qword. */
    x2 = _mm_clmulepi64_si128(x1, k128, 0x10);
    x1 = _mm_xor_si128(_mm_srli_si128(x1, 8), x2);
    /* 64 -> 32 bits: low 32 bits times x^64, xor the rest. */
    x2 = _mm_srli_si128(x1, 4);
    x1 = _mm_clmulepi64_si128(_mm_and_si128(x1, mask32), _mm_set_epi64x(0, XX_CRC32_ISO_K64), 0x00);
    x1 = _mm_xor_si128(x1, x2);
    /* Barrett reduction to the 32-bit remainder (bits 32..63). */
    {
        const __m128i barrett = _mm_set_epi64x(XX_CRC32_ISO_MU, XX_CRC32_ISO_P);
        x2 = _mm_clmulepi64_si128(_mm_and_si128(x1, mask32), barrett, 0x10);
        x2 = _mm_clmulepi64_si128(_mm_and_si128(x2, mask32), barrett, 0x00);
        x1 = _mm_xor_si128(x1, x2);
    }
    return (uint32_t)_mm_cvtsi128_si32(_mm_srli_si128(x1, 4));
}
#endif /* XX_CRC64_X86 */
#endif /* XX_CRC32_PCLMUL_H */
