/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_entropy_sse2.c
 * @brief Shannon entropy calculation with SSE2 histogram reduction.
 */

#include "xx_entropy_platform.h"
#include "xxfclib/rt/xx_rt.h"

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
#define XX_ENTROPY_SSE2_X86 1
#endif

#ifdef XX_ENTROPY_SSE2_X86
#if defined(_MSC_VER)
#include <intrin.h>
#include <emmintrin.h>
#else
#include <emmintrin.h>
#endif
#endif

#if defined(_MSC_VER)
#define XX_ENTROPY_SSE2_ALIGN16 __declspec(align(16))
#define XX_ENTROPY_SSE2_ALIGN64 __declspec(align(64))
#define XX_ENTROPY_SSE2_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define XX_ENTROPY_SSE2_ALIGN16 __attribute__((aligned(16)))
#define XX_ENTROPY_SSE2_ALIGN64 __attribute__((aligned(64)))
#define XX_ENTROPY_SSE2_NOINLINE __attribute__((noinline))
#else
#define XX_ENTROPY_SSE2_ALIGN16
#define XX_ENTROPY_SSE2_ALIGN64
#define XX_ENTROPY_SSE2_NOINLINE
#endif

#if defined(XX_ENTROPY_SSE2_X86) && (defined(__GNUC__) || defined(__clang__))
#define XX_ENTROPY_SSE2_TARGET __attribute__((target("sse2")))
#else
#define XX_ENTROPY_SSE2_TARGET
#endif

#define XX_ENTROPY_SSE2_LARGE_MIN_SIZE ((size_t)65536U)

static double xx_entropy_sse2_finish(const uint32_t total_counts[256], size_t size)
{
    double sum = 0.0;
    size_t i;

    for (i = 0U; i < 256U; ++i) {
        if (total_counts[i] != 0U) {
            double count = (double)total_counts[i];
            sum += count * xx_rt_log(count);
        }
    }
    return (xx_rt_log((double)size) - sum / (double)size) * 1.44269504088896340736;
}

#ifdef XX_ENTROPY_SSE2_X86
#define XX_ENTROPY_SSE2_COUNT8(q, offset, counts) \
    do {                                          \
        (counts)[0][(q)[(offset)]]++;             \
        (counts)[1][(q)[(offset) + 1U]]++;        \
        (counts)[2][(q)[(offset) + 2U]]++;        \
        (counts)[3][(q)[(offset) + 3U]]++;        \
        (counts)[4][(q)[(offset) + 4U]]++;        \
        (counts)[5][(q)[(offset) + 5U]]++;        \
        (counts)[6][(q)[(offset) + 6U]]++;        \
        (counts)[7][(q)[(offset) + 7U]]++;        \
    } while (0)

static XX_ENTROPY_SSE2_TARGET XX_ENTROPY_SSE2_NOINLINE void xx_entropy_histogram_sse2_small(const uint8_t *p, size_t size, uint32_t total_counts[256])
{
    XX_ENTROPY_SSE2_ALIGN16 uint32_t counts[4][256] = {{0}};
    size_t i = 0U;
    size_t limit = size & ~((size_t)15U);

    for (; i < limit; i += 16U) {
        counts[0][p[i]]++;
        counts[1][p[i + 1U]]++;
        counts[2][p[i + 2U]]++;
        counts[3][p[i + 3U]]++;
        counts[0][p[i + 4U]]++;
        counts[1][p[i + 5U]]++;
        counts[2][p[i + 6U]]++;
        counts[3][p[i + 7U]]++;
        counts[0][p[i + 8U]]++;
        counts[1][p[i + 9U]]++;
        counts[2][p[i + 10U]]++;
        counts[3][p[i + 11U]]++;
        counts[0][p[i + 12U]]++;
        counts[1][p[i + 13U]]++;
        counts[2][p[i + 14U]]++;
        counts[3][p[i + 15U]]++;
    }
    for (; i < size; ++i) counts[0][p[i]]++;

    for (i = 0U; i < 256U; i += 4U) {
        __m128i c0 = _mm_load_si128((const __m128i *)(counts[0] + i));
        __m128i c1 = _mm_load_si128((const __m128i *)(counts[1] + i));
        __m128i c2 = _mm_load_si128((const __m128i *)(counts[2] + i));
        __m128i c3 = _mm_load_si128((const __m128i *)(counts[3] + i));
        _mm_storeu_si128((__m128i *)(total_counts + i), _mm_add_epi32(_mm_add_epi32(c0, c1), _mm_add_epi32(c2, c3)));
    }
}

static XX_ENTROPY_SSE2_TARGET XX_ENTROPY_SSE2_NOINLINE void xx_entropy_histogram_sse2_large(const uint8_t *p, size_t size, uint32_t total_counts[256])
{
    XX_ENTROPY_SSE2_ALIGN64 uint32_t counts[8][256] = {{0}};
    size_t i = 0U;

    for (; size - i >= 64U; i += 64U) {
        const uint8_t *q = p + i;
        XX_ENTROPY_SSE2_COUNT8(q, 0U, counts);
        XX_ENTROPY_SSE2_COUNT8(q, 8U, counts);
        XX_ENTROPY_SSE2_COUNT8(q, 16U, counts);
        XX_ENTROPY_SSE2_COUNT8(q, 24U, counts);
        XX_ENTROPY_SSE2_COUNT8(q, 32U, counts);
        XX_ENTROPY_SSE2_COUNT8(q, 40U, counts);
        XX_ENTROPY_SSE2_COUNT8(q, 48U, counts);
        XX_ENTROPY_SSE2_COUNT8(q, 56U, counts);
    }
    for (; i < size; ++i) counts[0][p[i]]++;

    for (i = 0U; i < 256U; i += 4U) {
        __m128i c0 = _mm_load_si128((const __m128i *)(counts[0] + i));
        __m128i c1 = _mm_load_si128((const __m128i *)(counts[1] + i));
        __m128i c2 = _mm_load_si128((const __m128i *)(counts[2] + i));
        __m128i c3 = _mm_load_si128((const __m128i *)(counts[3] + i));
        __m128i c4 = _mm_load_si128((const __m128i *)(counts[4] + i));
        __m128i c5 = _mm_load_si128((const __m128i *)(counts[5] + i));
        __m128i c6 = _mm_load_si128((const __m128i *)(counts[6] + i));
        __m128i c7 = _mm_load_si128((const __m128i *)(counts[7] + i));
        __m128i c01 = _mm_add_epi32(c0, c1);
        __m128i c23 = _mm_add_epi32(c2, c3);
        __m128i c45 = _mm_add_epi32(c4, c5);
        __m128i c67 = _mm_add_epi32(c6, c7);
        _mm_storeu_si128((__m128i *)(total_counts + i), _mm_add_epi32(_mm_add_epi32(c01, c23), _mm_add_epi32(c45, c67)));
    }
}
#endif /* XX_ENTROPY_SSE2_X86 */

XX_ENTROPY_SSE2_TARGET
void xx_entropy_histogram_sse2(const void *data, size_t size, uint32_t total_counts[256])
{
    if (!total_counts) return;
    if (!data || size == 0U) {
        xx_rt_memset(total_counts, 0, 256U * sizeof(*total_counts));
        return;
    }

#ifdef XX_ENTROPY_SSE2_X86
    if (size >= XX_ENTROPY_SSE2_LARGE_MIN_SIZE) {
        xx_entropy_histogram_sse2_large((const uint8_t *)data, size, total_counts);
    } else {
        xx_entropy_histogram_sse2_small((const uint8_t *)data, size, total_counts);
    }
#else
    /* Non-x86 fallback */
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c0[256] = {0}, c1[256] = {0}, c2[256] = {0}, c3[256] = {0};
    size_t i = 0;
    size_t limit = size & ~((size_t)3);
    for (; i < limit; i += 4) {
        c0[p[i]]++;
        c1[p[i + 1]]++;
        c2[p[i + 2]]++;
        c3[p[i + 3]]++;
    }
    for (; i < size; i++) c0[p[i]]++;
    for (int k = 0; k < 256; k++) {
        total_counts[k] = c0[k] + c1[k] + c2[k] + c3[k];
    }
#endif
}

XX_ENTROPY_SSE2_TARGET
double xx_entropy_calculate_sse2(const void *data, size_t size)
{
    uint32_t total_counts[256];
    if (!data || size == 0U) return 0.0;
    xx_entropy_histogram_sse2(data, size, total_counts);
    return xx_entropy_sse2_finish(total_counts, size);
}

#ifdef XX_ENTROPY_SSE2_X86
#undef XX_ENTROPY_SSE2_COUNT8
#endif
#undef XX_ENTROPY_SSE2_LARGE_MIN_SIZE
#undef XX_ENTROPY_SSE2_TARGET
#undef XX_ENTROPY_SSE2_NOINLINE
#undef XX_ENTROPY_SSE2_ALIGN64
#undef XX_ENTROPY_SSE2_ALIGN16
#undef XX_ENTROPY_SSE2_X86
