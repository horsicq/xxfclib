/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_DEFLATE_DEC_PLATFORM_H
#define XX_DEFLATE_DEC_PLATFORM_H

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_DEFLATE_DEC_X86 1
#include <emmintrin.h>
#include <immintrin.h>
#endif

#if (defined(_M_ARM64) && !defined(_M_ARM64EC)) || defined(__aarch64__)
#define XX_DEFLATE_DEC_NEON 1
#include <arm_neon.h>
#endif

#if defined(_MSC_VER)
#define XX_DEFLATE_DEC_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define XX_DEFLATE_DEC_NOINLINE __attribute__((noinline))
#else
#define XX_DEFLATE_DEC_NOINLINE
#endif

/* The match copies are inlined into the target-specific block loops
 * (platforms/xx_deflate_dec_fast.inc instances). */
#if defined(_MSC_VER)
#define XX_DEFLATE_DEC_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define XX_DEFLATE_DEC_INLINE inline __attribute__((always_inline))
#else
#define XX_DEFLATE_DEC_INLINE inline
#endif

#if defined(XX_DEFLATE_DEC_X86) && (defined(__GNUC__) || defined(__clang__))
#define XX_DEFLATE_DEC_TARGET_SSE2 __attribute__((target("sse2")))
#define XX_DEFLATE_DEC_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_DEFLATE_DEC_TARGET_SSE2
#define XX_DEFLATE_DEC_TARGET_AVX2
#endif

#endif /* XX_DEFLATE_DEC_PLATFORM_H */
