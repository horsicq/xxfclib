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
 * @file xx_entropy_avx2.c
 * @brief AVX2 vectorized Shannon entropy calculation.
 */

#include "xx_entropy_platform.h"
#include "xxfclib/rt/xx_rt.h"

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
#  if defined(_MSC_VER)
#    include <intrin.h>
#    include <immintrin.h>
#  else
#    include <immintrin.h>
#  endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define XX_TARGET_AVX2 __attribute__((__target__("avx2")))
#  define XX_ALIGN32 __attribute__((aligned(32)))
#elif defined(_MSC_VER)
#  define XX_TARGET_AVX2
#  define XX_ALIGN32 __declspec(align(32))
#else
#  define XX_TARGET_AVX2
#  define XX_ALIGN32
#endif

XX_TARGET_AVX2
double xx_entropy_calculate_avx2(const void *data, size_t size) {
    if (!data || size == 0) {
        return 0.0;
    }

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))

    const uint8_t *p = (const uint8_t *)data;
    XX_ALIGN32 uint32_t c[8][256] = {{0}};

    size_t i = 0;
    size_t limit = size & ~((size_t)31);

    for (; i < limit; i += 32) {
        _mm_prefetch((const char *)(p + i + 512), _MM_HINT_T0);
        c[0][p[i]]++;     c[1][p[i+1]]++;   c[2][p[i+2]]++;   c[3][p[i+3]]++;
        c[4][p[i+4]]++;   c[5][p[i+5]]++;   c[6][p[i+6]]++;   c[7][p[i+7]]++;
        c[0][p[i+8]]++;   c[1][p[i+9]]++;   c[2][p[i+10]]++;  c[3][p[i+11]]++;
        c[4][p[i+12]]++;  c[5][p[i+13]]++;  c[6][p[i+14]]++;  c[7][p[i+15]]++;
        c[0][p[i+16]]++;  c[1][p[i+17]]++;  c[2][p[i+18]]++;  c[3][p[i+19]]++;
        c[4][p[i+20]]++;  c[5][p[i+21]]++;  c[6][p[i+22]]++;  c[7][p[i+23]]++;
        c[0][p[i+24]]++;  c[1][p[i+25]]++;  c[2][p[i+26]]++;  c[3][p[i+27]]++;
        c[4][p[i+28]]++;  c[5][p[i+29]]++;  c[6][p[i+30]]++;  c[7][p[i+31]]++;
    }
    for (; i < size; i++) {
        c[0][p[i]]++;
    }

    /* AVX2 256-bit vector horizontal reduction across 8 tables */
    uint32_t total_counts[256];
    for (int k = 0; k < 256; k += 8) {
        __m256i v0 = _mm256_load_si256((const __m256i *)&c[0][k]);
        __m256i v1 = _mm256_load_si256((const __m256i *)&c[1][k]);
        __m256i v2 = _mm256_load_si256((const __m256i *)&c[2][k]);
        __m256i v3 = _mm256_load_si256((const __m256i *)&c[3][k]);
        __m256i v4 = _mm256_load_si256((const __m256i *)&c[4][k]);
        __m256i v5 = _mm256_load_si256((const __m256i *)&c[5][k]);
        __m256i v6 = _mm256_load_si256((const __m256i *)&c[6][k]);
        __m256i v7 = _mm256_load_si256((const __m256i *)&c[7][k]);

        __m256i s01 = _mm256_add_epi32(v0, v1);
        __m256i s23 = _mm256_add_epi32(v2, v3);
        __m256i s45 = _mm256_add_epi32(v4, v5);
        __m256i s67 = _mm256_add_epi32(v6, v7);

        __m256i sum = _mm256_add_epi32(_mm256_add_epi32(s01, s23), _mm256_add_epi32(s45, s67));
        _mm256_storeu_si256((__m256i *)&total_counts[k], sum);
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
    return xx_entropy_calculate_sse2(data, size);
#endif
}
