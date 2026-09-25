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
 * @brief SSE2 vectorized Shannon entropy calculation.
 */

#include "xx_entropy_platform.h"
#include "xxfclib/rt/xx_rt.h"

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
#  if defined(_MSC_VER)
#    include <intrin.h>
#    include <emmintrin.h>
#  else
#    include <emmintrin.h>
#  endif
#endif

#if defined(_MSC_VER)
#  define XX_ALIGN16 __declspec(align(16))
#elif defined(__GNUC__) || defined(__clang__)
#  define XX_ALIGN16 __attribute__((aligned(16)))
#else
#  define XX_ALIGN16
#endif

double xx_entropy_calculate_sse2(const void *data, size_t size) {
    if (!data || size == 0) {
        return 0.0;
    }

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))

    const uint8_t *p = (const uint8_t *)data;
    XX_ALIGN16 uint32_t c[4][256] = {{0}};

    size_t i = 0;
    size_t limit = size & ~((size_t)15);

    for (; i < limit; i += 16) {
        _mm_prefetch((const char *)(p + i + 256), _MM_HINT_T0);
        c[0][p[i]]++;     c[1][p[i+1]]++;   c[2][p[i+2]]++;   c[3][p[i+3]]++;
        c[0][p[i+4]]++;   c[1][p[i+5]]++;   c[2][p[i+6]]++;   c[3][p[i+7]]++;
        c[0][p[i+8]]++;   c[1][p[i+9]]++;   c[2][p[i+10]]++;  c[3][p[i+11]]++;
        c[0][p[i+12]]++;  c[1][p[i+13]]++;  c[2][p[i+14]]++;  c[3][p[i+15]]++;
    }
    for (; i < size; i++) {
        c[0][p[i]]++;
    }

    /* SSE2 vector horizontal reduction across 4 tables */
    uint32_t total_counts[256];
    for (int k = 0; k < 256; k += 4) {
        __m128i v0 = _mm_load_si128((const __m128i *)&c[0][k]);
        __m128i v1 = _mm_load_si128((const __m128i *)&c[1][k]);
        __m128i v2 = _mm_load_si128((const __m128i *)&c[2][k]);
        __m128i v3 = _mm_load_si128((const __m128i *)&c[3][k]);
        __m128i sum = _mm_add_epi32(_mm_add_epi32(v0, v1), _mm_add_epi32(v2, v3));
        _mm_storeu_si128((__m128i *)&total_counts[k], sum);
    }

    double sum = 0.0;
    for (int k = 0; k < 256; k++) {
        if (total_counts[k] > 0) {
            double cnt = (double)total_counts[k];
            sum += cnt * xx_rt_log(cnt);
        }
    }

    double dsize = (double)size;
    double result = xx_rt_log(dsize) - (sum / dsize);
    const double inv_log2 = 1.44269504088896340736; /* 1.0 / ln(2.0) */

    return result * inv_log2;

#else
    /* Non-x86 fallback */
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c0[256] = {0}, c1[256] = {0}, c2[256] = {0}, c3[256] = {0};
    size_t i = 0;
    size_t limit = size & ~((size_t)3);
    for (; i < limit; i += 4) {
        c0[p[i]]++;   c1[p[i+1]]++; c2[p[i+2]]++; c3[p[i+3]]++;
    }
    for (; i < size; i++) c0[p[i]]++;
    double sum = 0.0;
    for (int k = 0; k < 256; k++) {
        uint64_t total = (uint64_t)c0[k] + c1[k] + c2[k] + c3[k];
        if (total > 0) {
            double cnt = (double)total;
            sum += cnt * xx_rt_log(cnt);
        }
    }
    double dsize = (double)size;
    return (xx_rt_log(dsize) - (sum / dsize)) * 1.44269504088896340736;
#endif
}
