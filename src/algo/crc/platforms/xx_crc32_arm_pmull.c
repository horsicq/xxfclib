/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Three independent 1024-byte CRC streams hide CRC instruction latency.
 * PMULL combines the first two remainders, then CRC64 reduces the product.
 * K(n) is the reflected remainder of x^(8*(n-4)), with identity 0x80000000.
 * Generate by shifting identity right 8*(n-4) times with the reflected
 * polynomial (IEEE 0xedb88320 or Castagnoli 0x82f63b78). The product is
 * shifted left one before CRC64 reduction to account for reflected bit order.
 * Entry requires BOTH CRC32 and PMULL enabled. Loads assemble little-endian words;
 * no reads extend past the supplied buffer. Public seed/final XOR preserved.
 */
#include <stdint.h>
#include <stddef.h>
#include "xx_crc32_arm.h"
#ifdef XX_CRC32_ARM
#if defined(_MSC_VER)
#include <intrin.h>
#include <arm64_neon.h>
#else
#include <arm_acle.h>
#include <arm_neon.h>
#endif

/* MSVC models the scalar polynomial operands of vmull_p64 as one-lane
 * vectors (__n64), while GCC and Clang use the scalar poly64_t type. */
#if defined(_MSC_VER)
#define XX_CRC_VMULL_P64(a, b) vmull_p64(vcreate_p64((uint64_t)(a)), vcreate_p64((uint64_t)(b)))
#else
#define XX_CRC_VMULL_P64(a, b) vmull_p64((poly64_t)(a), (poly64_t)(b))
#endif

static uint64_t xx_crc_pmull_read64(const uint8_t *p)
{
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8U) | ((uint64_t)p[2] << 16U) | ((uint64_t)p[3] << 24U) | ((uint64_t)p[4] << 32U) | ((uint64_t)p[5] << 40U) |
           ((uint64_t)p[6] << 48U) | ((uint64_t)p[7] << 56U);
}

uint32_t xx_crc32_arm_pmull(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!p || size == 0U) return crc;
    crc = ~crc;
    while (size >= 3072U) {
        uint32_t a = crc, b = 0, c = 0;
        size_t i;
        uint64_t x, y;
        for (i = 0; i < 1024U; i += 8U) {
            a = __crc32d(a, xx_crc_pmull_read64(p + i));
            b = __crc32d(b, xx_crc_pmull_read64(p + 1024U + i));
            c = __crc32d(c, xx_crc_pmull_read64(p + 2048U + i));
        }
        x = vgetq_lane_u64(vreinterpretq_u64_p128(XX_CRC_VMULL_P64(a, UINT32_C(0xd01dd77b))), 0);
        y = vgetq_lane_u64(vreinterpretq_u64_p128(XX_CRC_VMULL_P64(b, UINT32_C(0x5df97b6b))), 0);
        crc = __crc32d(0, (x ^ y) << 1U) ^ c;
        p += 3072U;
        size -= 3072U;
    }
    while (size >= 8U) {
        crc = __crc32d(crc, xx_crc_pmull_read64(p));
        p += 8U;
        size -= 8U;
    }
    while (size-- != 0U) crc = __crc32b(crc, *p++);
    return ~crc;
}

uint32_t xx_crc32c_arm_pmull(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!p || size == 0U) return crc;
    crc = ~crc;
    while (size >= 3072U) {
        uint32_t a = crc, b = 0, c = 0;
        size_t i;
        uint64_t x, y;
        for (i = 0; i < 1024U; i += 8U) {
            a = __crc32cd(a, xx_crc_pmull_read64(p + i));
            b = __crc32cd(b, xx_crc_pmull_read64(p + 1024U + i));
            c = __crc32cd(c, xx_crc_pmull_read64(p + 2048U + i));
        }
        x = vgetq_lane_u64(vreinterpretq_u64_p128(XX_CRC_VMULL_P64(a, UINT32_C(0xd07b8be2))), 0);
        y = vgetq_lane_u64(vreinterpretq_u64_p128(XX_CRC_VMULL_P64(b, UINT32_C(0x0b803b7d))), 0);
        crc = __crc32cd(0, (x ^ y) << 1U) ^ c;
        p += 3072U;
        size -= 3072U;
    }
    while (size >= 8U) {
        crc = __crc32cd(crc, xx_crc_pmull_read64(p));
        p += 8U;
        size -= 8U;
    }
    while (size-- != 0U) crc = __crc32cb(crc, *p++);
    return ~crc;
}

#undef XX_CRC_VMULL_P64

#endif /* XX_CRC32_ARM */
