/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_crc32_arm.h"

#ifdef XX_CRC32_ARM
#if defined(_MSC_VER)
#include <intrin.h>
#include <arm64_neon.h>
#define XX_CRC32_ARM_NOINLINE __declspec(noinline)
#else
#include <arm_acle.h>
#include <arm_neon.h>
#define XX_CRC32_ARM_NOINLINE __attribute__((noinline))
#endif

static uint32_t xx_crc32_arm_read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint64_t xx_crc32_arm_read64(const uint8_t *p)
{
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8U) | ((uint64_t)p[2] << 16U) | ((uint64_t)p[3] << 24U) | ((uint64_t)p[4] << 32U) | ((uint64_t)p[5] << 40U) |
           ((uint64_t)p[6] << 48U) | ((uint64_t)p[7] << 56U);
}

static uint32_t xx_crc32_arm_reverse32(uint32_t value)
{
    value = ((value >> 1U) & UINT32_C(0x55555555)) | ((value & UINT32_C(0x55555555)) << 1U);
    value = ((value >> 2U) & UINT32_C(0x33333333)) | ((value & UINT32_C(0x33333333)) << 2U);
    value = ((value >> 4U) & UINT32_C(0x0f0f0f0f)) | ((value & UINT32_C(0x0f0f0f0f)) << 4U);
    value = ((value >> 8U) & UINT32_C(0x00ff00ff)) | ((value & UINT32_C(0x00ff00ff)) << 8U);
    return (value >> 16U) | (value << 16U);
}

static uint8_t xx_crc32_arm_reverse8(uint8_t value)
{
    value = (uint8_t)(((value >> 1U) & 0x55U) | ((value & 0x55U) << 1U));
    value = (uint8_t)(((value >> 2U) & 0x33U) | ((value & 0x33U) << 2U));
    return (uint8_t)((value >> 4U) | (value << 4U));
}

XX_CRC32_ARM_NOINLINE
uint32_t xx_crc32_arm(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!p || size == 0U) return crc;
    crc = ~crc;
    while (size >= 8U) {
        crc = __crc32d(crc, xx_crc32_arm_read64(p));
        p += 8U;
        size -= 8U;
    }
    if (size >= 4U) {
        crc = __crc32w(crc, xx_crc32_arm_read32(p));
        p += 4U;
        size -= 4U;
    }
    if (size >= 2U) {
        crc = __crc32h(crc, (uint32_t)p[0] | ((uint32_t)p[1] << 8U));
        p += 2U;
        size -= 2U;
    }
    if (size != 0U) crc = __crc32b(crc, *p);
    return ~crc;
}

XX_CRC32_ARM_NOINLINE
uint32_t xx_crc32c_arm(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!p || size == 0U) return crc;
    crc = ~crc;
    while (size >= 8U) {
        crc = __crc32cd(crc, xx_crc32_arm_read64(p));
        p += 8U;
        size -= 8U;
    }
    if (size >= 4U) {
        crc = __crc32cw(crc, xx_crc32_arm_read32(p));
        p += 4U;
        size -= 4U;
    }
    if (size >= 2U) {
        crc = __crc32ch(crc, (uint32_t)p[0] | ((uint32_t)p[1] << 8U));
        p += 2U;
        size -= 2U;
    }
    if (size != 0U) crc = __crc32cb(crc, *p);
    return ~crc;
}

XX_CRC32_ARM_NOINLINE
uint32_t xx_crc32_mpeg2_arm(uint32_t reg, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t reflected;

    if (!p || size == 0U) return reg;
    reflected = xx_crc32_arm_reverse32(reg);
#if !defined(__BYTE_ORDER__) || (__BYTE_ORDER__ != __ORDER_BIG_ENDIAN__)
    while (size >= 16U) {
        uint64x2_t words = vreinterpretq_u64_u8(vrbitq_u8(vld1q_u8(p)));
        reflected = __crc32d(reflected, vgetq_lane_u64(words, 0));
        reflected = __crc32d(reflected, vgetq_lane_u64(words, 1));
        p += 16U;
        size -= 16U;
    }
    if (size >= 8U) {
        uint64x1_t word = vreinterpret_u64_u8(vrbit_u8(vld1_u8(p)));
        reflected = __crc32d(reflected, vget_lane_u64(word, 0));
        p += 8U;
        size -= 8U;
    }
#endif
    while (size-- != 0U) {
        reflected = __crc32b(reflected, xx_crc32_arm_reverse8(*p++));
    }
    return xx_crc32_arm_reverse32(reflected);
}
#endif /* XX_CRC32_ARM */
