/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_DEFLATE_PLATFORM_H
#define XX_DEFLATE_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef _MSC_VER
#include <intrin.h>
#define XX_DEFLATE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define XX_DEFLATE_INLINE inline __attribute__((always_inline))
#else
#define XX_DEFLATE_INLINE inline
#endif

typedef size_t (*xx_deflate_match_function)(const uint8_t *, const uint8_t *, size_t);
size_t xx_deflate_match_sse2(const uint8_t *, const uint8_t *, size_t);
size_t xx_deflate_match_avx2(const uint8_t *, const uint8_t *, size_t);

/* No load crosses the supplied extent, including an allocation/page end.
 * memcpy gives unaligned word loads without violating C aliasing rules. */
static XX_DEFLATE_INLINE size_t xx_deflate_match_words(const uint8_t *a, const uint8_t *b, size_t maximum)
{
    size_t at = 0;
    while (maximum - at >= 8U) {
        uint64_t first, second, difference;
        memcpy(&first, a + at, sizeof(first));
        memcpy(&second, b + at, sizeof(second));
        difference = first ^ second;
        if (difference != 0U) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
            unsigned long bit;
            _BitScanForward64(&bit, difference);
            return at + bit / 8U;
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ && (defined(__GNUC__) || defined(__clang__))
            return at + (unsigned)__builtin_ctzll(difference) / 8U;
#else
            while (a[at] == b[at]) ++at;
            return at;
#endif
        }
        at += 8U;
    }
    while (at < maximum && a[at] == b[at]) ++at;
    return at;
}

#endif
