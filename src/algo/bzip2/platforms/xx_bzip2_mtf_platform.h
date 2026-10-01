/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_BZIP2_MTF_PLATFORM_H
#define XX_BZIP2_MTF_PLATFORM_H

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_BZIP2_MTF_X86 1
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif
#endif

#if defined(_MSC_VER)
#define XX_BZIP2_MTF_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define XX_BZIP2_MTF_NOINLINE __attribute__((noinline))
#else
#define XX_BZIP2_MTF_NOINLINE
#endif

#if defined(XX_BZIP2_MTF_X86) && (defined(__GNUC__) || defined(__clang__))
#define XX_BZIP2_MTF_TARGET_SSE2 __attribute__((target("sse2")))
#else
#define XX_BZIP2_MTF_TARGET_SSE2
#endif

#endif /* XX_BZIP2_MTF_PLATFORM_H */
