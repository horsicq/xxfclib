/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_memory_platform.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_MEMORY_X86
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_MEMORY_X86)
#define XX_MEMORY_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_MEMORY_TARGET_AVX2
#endif

XX_MEMORY_TARGET_AVX2
void xx_memory_copy_avx2(uint8_t *destination, const uint8_t *source, size_t size) {
#ifdef XX_MEMORY_X86
    size_t at = 0;
    for (; size - at >= 128; at += 128) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(source + at));
        __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(source + at + 32));
        __m256i c = _mm256_loadu_si256((const __m256i *)(const void *)(source + at + 64));
        __m256i d = _mm256_loadu_si256((const __m256i *)(const void *)(source + at + 96));
        _mm256_storeu_si256((__m256i *)(void *)(destination + at), a);
        _mm256_storeu_si256((__m256i *)(void *)(destination + at + 32), b);
        _mm256_storeu_si256((__m256i *)(void *)(destination + at + 64), c);
        _mm256_storeu_si256((__m256i *)(void *)(destination + at + 96), d);
    }
    for (; size - at >= 32; at += 32) {
        __m256i value = _mm256_loadu_si256((const __m256i *)(const void *)(source + at));
        _mm256_storeu_si256((__m256i *)(void *)(destination + at), value);
    }
    if (size - at >= 16) {
        __m128i value = _mm_loadu_si128((const __m128i *)(const void *)(source + at));
        _mm_storeu_si128((__m128i *)(void *)(destination + at), value);
        at += 16;
    }
    /* Volatile tails stay bounded and do not become CRT memcpy calls. */
    for (; at < size; ++at)
        ((volatile uint8_t *)destination)[at] = ((const volatile uint8_t *)source)[at];
    _mm256_zeroupper();
#else
    xx_memory_copy_scalar(destination, source, size);
#endif
}

XX_MEMORY_TARGET_AVX2
void xx_memory_move_avx2(uint8_t *destination, const uint8_t *source, size_t size) {
#ifdef XX_MEMORY_X86
    uintptr_t d = (uintptr_t)destination, s = (uintptr_t)source;
    size_t at;
    if (size == 0 || d == s) return;
    if (d > s && d - s < size) {
        at = size;
        while (at >= 32) {
            __m256i value;
            at -= 32;
            value = _mm256_loadu_si256((const __m256i *)(const void *)(source + at));
            _mm256_storeu_si256((__m256i *)(void *)(destination + at), value);
        }
        _mm256_zeroupper();
        xx_memory_move_scalar(destination, source, at);
    } else if (d < s && s - d < size) {
        at = 0;
        for (; size - at >= 32; at += 32) {
            __m256i value = _mm256_loadu_si256((const __m256i *)(const void *)(source + at));
            _mm256_storeu_si256((__m256i *)(void *)(destination + at), value);
        }
        _mm256_zeroupper();
        xx_memory_move_scalar(destination + at, source + at, size - at);
    } else {
        xx_memory_copy_avx2(destination, source, size);
    }
#else
    xx_memory_move_scalar(destination, source, size);
#endif
}

XX_MEMORY_TARGET_AVX2
void xx_memory_set_avx2(uint8_t *destination, uint8_t value, size_t size) {
#ifdef XX_MEMORY_X86
    __m256i bytes = _mm256_set1_epi8((char)value);
    size_t at = 0;
    for (; size - at >= 128; at += 128) {
        _mm256_storeu_si256((__m256i *)(void *)(destination + at), bytes);
        _mm256_storeu_si256((__m256i *)(void *)(destination + at + 32), bytes);
        _mm256_storeu_si256((__m256i *)(void *)(destination + at + 64), bytes);
        _mm256_storeu_si256((__m256i *)(void *)(destination + at + 96), bytes);
    }
    for (; size - at >= 32; at += 32)
        _mm256_storeu_si256((__m256i *)(void *)(destination + at), bytes);
    _mm256_zeroupper();
    xx_memory_set_scalar(destination + at, value, size - at);
#else
    xx_memory_set_scalar(destination, value, size);
#endif
}

XX_MEMORY_TARGET_AVX2
int xx_memory_compare_avx2(const uint8_t *first, const uint8_t *second, size_t size) {
#ifdef XX_MEMORY_X86
    size_t at = 0;
    for (; size - at >= 32; at += 32) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(first + at));
        __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(second + at));
        unsigned mismatch = ~(unsigned)_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, b));
        if (mismatch != 0) {
            int result;
#ifdef _MSC_VER
            unsigned long bit;
            _BitScanForward(&bit, mismatch);
#else
            unsigned bit = (unsigned)__builtin_ctz(mismatch);
#endif
            result = (int)first[at + bit] - (int)second[at + bit];
            _mm256_zeroupper();
            return result;
        }
    }
    _mm256_zeroupper();
    return xx_memory_compare_scalar(first + at, second + at, size - at);
#else
    return xx_memory_compare_scalar(first, second, size);
#endif
}

XX_MEMORY_TARGET_AVX2
const uint8_t *xx_memory_find_avx2(const uint8_t *source, uint8_t value, size_t size) {
#ifdef XX_MEMORY_X86
    __m256i wanted = _mm256_set1_epi8((char)value);
    size_t at = 0;
    for (; size - at >= 32; at += 32) {
        __m256i bytes = _mm256_loadu_si256((const __m256i *)(const void *)(source + at));
        unsigned matches = (unsigned)_mm256_movemask_epi8(_mm256_cmpeq_epi8(bytes, wanted));
        if (matches != 0) {
#ifdef _MSC_VER
            unsigned long bit;
            _BitScanForward(&bit, matches);
#else
            unsigned bit = (unsigned)__builtin_ctz(matches);
#endif
            _mm256_zeroupper();
            return source + at + bit;
        }
    }
    _mm256_zeroupper();
    return xx_memory_find_scalar(source + at, value, size - at);
#else
    return xx_memory_find_scalar(source, value, size);
#endif
}
