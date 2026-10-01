/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_memory_platform.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_MEMORY_X86
#include <emmintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_MEMORY_X86)
#define XX_MEMORY_TARGET_SSE2 __attribute__((target("sse2")))
#else
#define XX_MEMORY_TARGET_SSE2
#endif

XX_MEMORY_TARGET_SSE2
void xx_memory_copy_sse2(uint8_t *destination, const uint8_t *source, size_t size) {
#ifdef XX_MEMORY_X86
    size_t at = 0;
    for (; size - at >= 64; at += 64) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(source + at));
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(source + at + 16));
        __m128i c = _mm_loadu_si128((const __m128i *)(const void *)(source + at + 32));
        __m128i d = _mm_loadu_si128((const __m128i *)(const void *)(source + at + 48));
        _mm_storeu_si128((__m128i *)(void *)(destination + at), a);
        _mm_storeu_si128((__m128i *)(void *)(destination + at + 16), b);
        _mm_storeu_si128((__m128i *)(void *)(destination + at + 32), c);
        _mm_storeu_si128((__m128i *)(void *)(destination + at + 48), d);
    }
    for (; size - at >= 16; at += 16) {
        __m128i value = _mm_loadu_si128((const __m128i *)(const void *)(source + at));
        _mm_storeu_si128((__m128i *)(void *)(destination + at), value);
    }
    /* Volatile tails stay bounded and do not become CRT memcpy calls. */
    for (; at < size; ++at)
        ((volatile uint8_t *)destination)[at] = ((const volatile uint8_t *)source)[at];
#else
    xx_memory_copy_scalar(destination, source, size);
#endif
}

XX_MEMORY_TARGET_SSE2
void xx_memory_move_sse2(uint8_t *destination, const uint8_t *source, size_t size) {
#ifdef XX_MEMORY_X86
    uintptr_t d = (uintptr_t)destination, s = (uintptr_t)source;
    size_t at;
    if (size == 0 || d == s) return;
    if (d > s && d - s < size) {
        at = size;
        while (at >= 16) {
            __m128i value;
            at -= 16;
            value = _mm_loadu_si128((const __m128i *)(const void *)(source + at));
            _mm_storeu_si128((__m128i *)(void *)(destination + at), value);
        }
        xx_memory_move_scalar(destination, source, at);
    } else if (d < s && s - d < size) {
        at = 0;
        for (; size - at >= 16; at += 16) {
            __m128i value = _mm_loadu_si128((const __m128i *)(const void *)(source + at));
            _mm_storeu_si128((__m128i *)(void *)(destination + at), value);
        }
        xx_memory_move_scalar(destination + at, source + at, size - at);
    } else {
        xx_memory_copy_sse2(destination, source, size);
    }
#else
    xx_memory_move_scalar(destination, source, size);
#endif
}

XX_MEMORY_TARGET_SSE2
void xx_memory_set_sse2(uint8_t *destination, uint8_t value, size_t size) {
#ifdef XX_MEMORY_X86
    __m128i bytes = _mm_set1_epi8((char)value);
    size_t at = 0;
    for (; size - at >= 64; at += 64) {
        _mm_storeu_si128((__m128i *)(void *)(destination + at), bytes);
        _mm_storeu_si128((__m128i *)(void *)(destination + at + 16), bytes);
        _mm_storeu_si128((__m128i *)(void *)(destination + at + 32), bytes);
        _mm_storeu_si128((__m128i *)(void *)(destination + at + 48), bytes);
    }
    for (; size - at >= 16; at += 16)
        _mm_storeu_si128((__m128i *)(void *)(destination + at), bytes);
    xx_memory_set_scalar(destination + at, value, size - at);
#else
    xx_memory_set_scalar(destination, value, size);
#endif
}

XX_MEMORY_TARGET_SSE2
int xx_memory_compare_sse2(const uint8_t *first, const uint8_t *second, size_t size) {
#ifdef XX_MEMORY_X86
    size_t at = 0;
    for (; size - at >= 16; at += 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(first + at));
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(second + at));
        unsigned mismatch = (~(unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(a, b))) & 0xffffU;
        if (mismatch != 0) {
#ifdef _MSC_VER
            unsigned long bit;
            _BitScanForward(&bit, mismatch);
#else
            unsigned bit = (unsigned)__builtin_ctz(mismatch);
#endif
            return (int)first[at + bit] - (int)second[at + bit];
        }
    }
    return xx_memory_compare_scalar(first + at, second + at, size - at);
#else
    return xx_memory_compare_scalar(first, second, size);
#endif
}

XX_MEMORY_TARGET_SSE2
const uint8_t *xx_memory_find_sse2(const uint8_t *source, uint8_t value, size_t size) {
#ifdef XX_MEMORY_X86
    __m128i wanted = _mm_set1_epi8((char)value);
    size_t at = 0;
    for (; size - at >= 16; at += 16) {
        __m128i bytes = _mm_loadu_si128((const __m128i *)(const void *)(source + at));
        unsigned matches = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, wanted));
        if (matches != 0) {
#ifdef _MSC_VER
            unsigned long bit;
            _BitScanForward(&bit, matches);
#else
            unsigned bit = (unsigned)__builtin_ctz(matches);
#endif
            return source + at + bit;
        }
    }
    return xx_memory_find_scalar(source + at, value, size - at);
#else
    return xx_memory_find_scalar(source, value, size);
#endif
}
