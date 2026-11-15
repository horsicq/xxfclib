/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_MASKSCAN_PLATFORM_H
#define XX_MASKSCAN_PLATFORM_H

#include "xxfclib/algo/maskscan/xx_maskscan.h"

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)) && !defined(_M_ARM64EC)
#define XX_MASKSCAN_X86 1
#include <immintrin.h>
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_MASKSCAN_X86)
#define XX_MASKSCAN_TARGET_SSE2 __attribute__((target("sse2")))
#define XX_MASKSCAN_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_MASKSCAN_TARGET_SSE2
#define XX_MASKSCAN_TARGET_AVX2
#endif

#if defined(_MSC_VER)
#include <intrin.h>
static __inline unsigned xx_maskscan_first_bit(uint32_t bits)
{
    unsigned long index;
    _BitScanForward(&index, bits);
    return (unsigned)index;
}
#else
static inline unsigned xx_maskscan_first_bit(uint32_t bits)
{
    return (unsigned)__builtin_ctz(bits);
}
#endif

/* Backends receive validated arguments: 1 <= count <= XX_MASKSCAN_MAX_PROBES,
 * first <= last and last + every offset inside the buffer. */
size_t xx_maskscan_find_scalar(const uint8_t *data, size_t first, size_t last,
                               const xx_maskscan_probe *probes, size_t count);

#ifdef XX_MASKSCAN_X86
size_t xx_maskscan_find_sse2(const uint8_t *data, size_t first, size_t last,
                             const xx_maskscan_probe *probes, size_t count);
size_t xx_maskscan_find_avx2(const uint8_t *data, size_t first, size_t last,
                             const xx_maskscan_probe *probes, size_t count);
#endif

#endif /* XX_MASKSCAN_PLATFORM_H */
