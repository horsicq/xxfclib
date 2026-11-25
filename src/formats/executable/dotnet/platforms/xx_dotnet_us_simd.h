/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_DOTNET_US_SIMD_H
#define XX_DOTNET_US_SIMD_H

#include <stddef.h>
#include <stdint.h>

#if defined(_M_IX86) || (defined(_M_X64) && !defined(_M_ARM64EC)) || defined(__i386__) || (defined(__x86_64__) && !defined(_M_ARM64EC))
#define XX_DOTNET_HAVE_X86_SIMD 1

/* Convert complete vectors of ASCII UTF-16LE code units to bytes. Processing
 * stops before the first vector containing a non-ASCII unit. The caller owns
 * runtime CPU dispatch and handles the scalar Unicode fallback. */
size_t xx_dotnet_us_ascii_sse2(const uint8_t *source, size_t units, uint8_t *destination, size_t capacity);
size_t xx_dotnet_us_ascii_avx2(const uint8_t *source, size_t units, uint8_t *destination, size_t capacity);
#else
#define XX_DOTNET_HAVE_X86_SIMD 0
#endif

#endif /* XX_DOTNET_US_SIMD_H */
