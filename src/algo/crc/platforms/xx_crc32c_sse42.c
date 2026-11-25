/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_crc32c_sse42.h"

#ifdef XX_CRC32C_SSE42_X86
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <nmmintrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define XX_CRC32C_TARGET_SSE42 __attribute__((target("sse4.2")))
#else
#define XX_CRC32C_TARGET_SSE42
#endif

/* Assemble unaligned little-endian words without type-punning or alignment
 * assumptions. Compilers can fold these byte reads into a single load. */
static uint32_t xx_crc32c_read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#if defined(_M_X64) || defined(__x86_64__)
static uint64_t xx_crc32c_read64(const uint8_t *p)
{
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}
#endif

XX_CRC32C_TARGET_SSE42
uint32_t xx_crc32c_sse42(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!p || size == 0) return crc;
    crc = ~crc;
#if defined(_M_X64) || defined(__x86_64__)
    while (size >= 8U) {
        crc = (uint32_t)_mm_crc32_u64(crc, xx_crc32c_read64(p));
        p += 8U;
        size -= 8U;
    }
#endif
    while (size >= 4U) {
        crc = _mm_crc32_u32(crc, xx_crc32c_read32(p));
        p += 4U;
        size -= 4U;
    }
    while (size-- != 0U) crc = _mm_crc32_u8(crc, *p++);
    return ~crc;
}
#endif /* XX_CRC32C_SSE42_X86 */
