/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/algo/literal_nocase/xx_literal_nocase.h"
#include "xxfclib/global/xx_global.h"

#if !defined(_M_ARM64EC) && (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__))
#define XX_LITERAL_NOCASE_X86 1
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

#if defined(XX_LITERAL_NOCASE_X86) && (defined(__GNUC__) || defined(__clang__))
#define XX_LITERAL_SSE2 __attribute__((target("sse2"), noinline))
#define XX_LITERAL_AVX2 __attribute__((target("avx2"), noinline))
#elif defined(_MSC_VER)
#define XX_LITERAL_SSE2 __declspec(noinline)
#define XX_LITERAL_AVX2 __declspec(noinline)
#else
#define XX_LITERAL_SSE2
#define XX_LITERAL_AVX2
#endif

static uint8_t xx_literal_fold(uint8_t byte)
{
    return byte >= 'A' && byte <= 'Z' ? (uint8_t)(byte + 32) : byte;
}

static bool xx_literal_nocase_equal(const uint8_t *data,
                                    const uint8_t *needle, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (xx_literal_fold(data[i]) != xx_literal_fold(needle[i])) return false;
    return true;
}

size_t xx_literal_nocase_find_scalar(const uint8_t *data, size_t size,
                                     size_t start, const uint8_t *needle,
                                     size_t length)
{
    if (!data || !needle || !length || length > size || start > size - length)
        return SIZE_MAX;
    size_t count = size - length + 1;
    for (size_t i = start; i < count; ++i)
        if (xx_literal_nocase_equal(data + i, needle, length)) return i;
    return SIZE_MAX;
}

#if defined(XX_LITERAL_NOCASE_X86)
static unsigned xx_literal_first_bit(uint32_t mask)
{
#if defined(_MSC_VER)
    unsigned long bit;
    _BitScanForward(&bit, mask);
    return (unsigned)bit;
#else
    return (unsigned)__builtin_ctz(mask);
#endif
}

/* Last nonzero byte is useful for wide literals, whose final byte is usually
 * zero. The first and second pivots are only a filter: full verification is
 * always performed, and candidate starts are visited in increasing order. */
static size_t xx_literal_second_pivot(const uint8_t *needle, size_t length)
{
    size_t pivot = length - 1;
    while (pivot && needle[pivot] == 0) --pivot;
    return pivot ? pivot : length - 1;
}

XX_LITERAL_SSE2
static size_t xx_literal_nocase_sse2(const uint8_t *data, size_t size,
                                     size_t start, const uint8_t *needle,
                                     size_t length)
{
    size_t count = size - length + 1;
    size_t pivot = xx_literal_second_pivot(needle, length);
    uint8_t first = xx_literal_fold(needle[0]);
    uint8_t second = xx_literal_fold(needle[pivot]);
    __m128i first_value = _mm_set1_epi8((char)first);
    __m128i second_value = _mm_set1_epi8((char)second);
    __m128i first_fold = _mm_set1_epi8(first >= 'a' && first <= 'z' ? 32 : 0);
    __m128i second_fold = _mm_set1_epi8(second >= 'a' && second <= 'z' ? 32 : 0);
    size_t i = start;
    for (; count - i >= 16; i += 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(data + i));
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(data + i + pivot));
        __m128i a_hit = _mm_cmpeq_epi8(_mm_or_si128(a, first_fold), first_value);
        __m128i b_hit = _mm_cmpeq_epi8(_mm_or_si128(b, second_fold), second_value);
        uint32_t mask = (uint32_t)_mm_movemask_epi8(_mm_and_si128(a_hit, b_hit));
        while (mask) {
            size_t position = i + xx_literal_first_bit(mask);
            mask &= mask - 1;
            if (xx_literal_nocase_equal(data + position, needle, length)) return position;
        }
    }
    return xx_literal_nocase_find_scalar(data, size, i, needle, length);
}

/* Deliberately isolated behind noinline and runtime CPU/OS dispatch. Do not
 * compile the dispatcher with a global AVX2 architecture requirement. */
XX_LITERAL_AVX2
static size_t xx_literal_nocase_avx2(const uint8_t *data, size_t size,
                                     size_t start, const uint8_t *needle,
                                     size_t length)
{
    size_t count = size - length + 1;
    size_t pivot = xx_literal_second_pivot(needle, length);
    uint8_t first = xx_literal_fold(needle[0]);
    uint8_t second = xx_literal_fold(needle[pivot]);
    __m256i first_value = _mm256_set1_epi8((char)first);
    __m256i second_value = _mm256_set1_epi8((char)second);
    __m256i first_fold = _mm256_set1_epi8(first >= 'a' && first <= 'z' ? 32 : 0);
    __m256i second_fold = _mm256_set1_epi8(second >= 'a' && second <= 'z' ? 32 : 0);
    size_t i = start;
    for (; count - i >= 32; i += 32) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(data + i));
        __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(data + i + pivot));
        __m256i a_hit = _mm256_cmpeq_epi8(_mm256_or_si256(a, first_fold), first_value);
        __m256i b_hit = _mm256_cmpeq_epi8(_mm256_or_si256(b, second_fold), second_value);
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(_mm256_and_si256(a_hit, b_hit));
        while (mask) {
            size_t position = i + xx_literal_first_bit(mask);
            mask &= mask - 1;
            if (xx_literal_nocase_equal(data + position, needle, length)) return position;
        }
    }
    return xx_literal_nocase_find_scalar(data, size, i, needle, length);
}
#endif

size_t xx_literal_nocase_find(const uint8_t *data, size_t size, size_t start,
                              const uint8_t *needle, size_t length)
{
    if (!data || !needle || !length || length > size || start > size - length)
        return SIZE_MAX;
    /* Repeated callers commonly resume directly on another overlapping hit.
     * Avoid rebuilding SIMD constants and dispatching for that case. */
    if (xx_literal_nocase_equal(data + start, needle, length)) return start;
    if (start == size - length) return SIZE_MAX;
    ++start;
#if defined(XX_LITERAL_NOCASE_X86)
    size_t remaining = size - length - start + 1;
    if (remaining >= 32 && xx_is_avx2_enabled())
        return xx_literal_nocase_avx2(data, size, start, needle, length);
    if (remaining >= 16 && xx_is_sse2_enabled())
        return xx_literal_nocase_sse2(data, size, start, needle, length);
#endif
    return xx_literal_nocase_find_scalar(data, size, start, needle, length);
}
