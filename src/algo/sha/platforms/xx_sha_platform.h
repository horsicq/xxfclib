/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SHA_PLATFORM_H
#define XX_SHA_PLATFORM_H

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_SHA_X86 1
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#endif
#endif

#if defined(_MSC_VER)
#define XX_SHA_INLINE __forceinline
#define XX_SHA_NOINLINE __declspec(noinline)
#else
#define XX_SHA_INLINE inline
#if defined(__GNUC__) || defined(__clang__)
#define XX_SHA_NOINLINE __attribute__((noinline))
#else
#define XX_SHA_NOINLINE
#endif
#endif

#if defined(XX_SHA_X86) && (defined(__GNUC__) || defined(__clang__))
#define XX_SHA_TARGET_SSE2 __attribute__((target("sse2")))
#define XX_SHA_TARGET_SHA_NI __attribute__((target("sse2,sha")))
#else
#define XX_SHA_TARGET_SSE2
#define XX_SHA_TARGET_SHA_NI
#endif

#if defined(XX_SHA_X86) && ((defined(_MSC_VER) && _MSC_VER >= 1910) || defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 5))
#define XX_SHA_HAVE_SHA_NI 1
#endif

/* Disabled until the backend has been executed against the scalar oracle
 * on a SHA-capable processor or an instruction emulator. SSE2 is always
 * available for independent validation on ordinary x86 systems. */
#ifndef XX_SHA_ENABLE_SHA_NI_BACKEND
#define XX_SHA_ENABLE_SHA_NI_BACKEND 0
#endif

#endif /* XX_SHA_PLATFORM_H */
