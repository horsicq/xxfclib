/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_ADLER32_PLATFORM_H
#define XX_ADLER32_PLATFORM_H

#include "xxfclib/algo/adler32/xx_adler32.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_ADLER32_X86 1
#include <immintrin.h>
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_ADLER32_X86)
#define XX_ADLER32_TARGET_SSE2 __attribute__((target("sse2")))
#define XX_ADLER32_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_ADLER32_TARGET_SSE2
#define XX_ADLER32_TARGET_AVX2
#endif

uint32_t xx_adler32_scalar(uint32_t adler, const void *data, size_t size);
uint32_t xx_adler32_sse2(uint32_t adler, const void *data, size_t size);
uint32_t xx_adler32_avx2(uint32_t adler, const void *data, size_t size);

#endif /* XX_ADLER32_PLATFORM_H */
