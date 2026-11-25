/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_entropy_platform.h"

#ifdef XX_ENTROPY_NEON
#include "xxfclib/rt/xx_rt.h"
#include <arm_neon.h>

#if defined(_MSC_VER)
#define XX_ENTROPY_NEON_ALIGN64 __declspec(align(64))
#define XX_ENTROPY_NEON_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define XX_ENTROPY_NEON_ALIGN64 __attribute__((aligned(64)))
#define XX_ENTROPY_NEON_NOINLINE __attribute__((noinline))
#else
#define XX_ENTROPY_NEON_ALIGN64
#define XX_ENTROPY_NEON_NOINLINE
#endif

#define XX_ENTROPY_NEON_LARGE_MIN_SIZE ((size_t)65536U)

#define XX_ENTROPY_NEON_COUNT8(q, offset, h0, h1, h2, h3, h4, h5, h6, h7) \
    do {                                                                  \
        (h0)[(q)[(offset)]]++;                                            \
        (h1)[(q)[(offset) + 1U]]++;                                       \
        (h2)[(q)[(offset) + 2U]]++;                                       \
        (h3)[(q)[(offset) + 3U]]++;                                       \
        (h4)[(q)[(offset) + 4U]]++;                                       \
        (h5)[(q)[(offset) + 5U]]++;                                       \
        (h6)[(q)[(offset) + 6U]]++;                                       \
        (h7)[(q)[(offset) + 7U]]++;                                       \
    } while (0)

static double xx_entropy_neon_finish(const uint32_t total_counts[256], size_t size)
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

static XX_ENTROPY_NEON_NOINLINE void xx_entropy_histogram_neon_large(const uint8_t *p, size_t size, uint32_t total_counts[256])
{
    XX_ENTROPY_NEON_ALIGN64 uint32_t counts[8][256] = {{0}};
    uint32_t *h0 = counts[0];
    uint32_t *h1 = counts[1];
    uint32_t *h2 = counts[2];
    uint32_t *h3 = counts[3];
    uint32_t *h4 = counts[4];
    uint32_t *h5 = counts[5];
    uint32_t *h6 = counts[6];
    uint32_t *h7 = counts[7];
    size_t i = 0U;

    /* Eight independent histograms break the indexed read/modify/write
     * dependency chains in the hot loop. A 64-byte unroll was faster than
     * prefetching or wider histogram sets on ARM64. */
    for (; size - i >= 64U; i += 64U) {
        const uint8_t *q = p + i;
        XX_ENTROPY_NEON_COUNT8(q, 0U, h0, h1, h2, h3, h4, h5, h6, h7);
        XX_ENTROPY_NEON_COUNT8(q, 8U, h0, h1, h2, h3, h4, h5, h6, h7);
        XX_ENTROPY_NEON_COUNT8(q, 16U, h0, h1, h2, h3, h4, h5, h6, h7);
        XX_ENTROPY_NEON_COUNT8(q, 24U, h0, h1, h2, h3, h4, h5, h6, h7);
        XX_ENTROPY_NEON_COUNT8(q, 32U, h0, h1, h2, h3, h4, h5, h6, h7);
        XX_ENTROPY_NEON_COUNT8(q, 40U, h0, h1, h2, h3, h4, h5, h6, h7);
        XX_ENTROPY_NEON_COUNT8(q, 48U, h0, h1, h2, h3, h4, h5, h6, h7);
        XX_ENTROPY_NEON_COUNT8(q, 56U, h0, h1, h2, h3, h4, h5, h6, h7);
    }
    for (; i < size; ++i) h0[p[i]]++;

    for (i = 0U; i < 256U; ++i) {
        total_counts[i] = h0[i] + h1[i] + h2[i] + h3[i] + h4[i] + h5[i] + h6[i] + h7[i];
    }
}

static XX_ENTROPY_NEON_NOINLINE void xx_entropy_histogram_neon_small(const uint8_t *p, size_t size, uint32_t total_counts[256])
{
    uint32_t counts[4][256] = {{0}};
    size_t i = 0;

    for (; size - i >= 16U; i += 16U) {
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
        uint32x4_t c0 = vld1q_u32(counts[0] + i);
        uint32x4_t c1 = vld1q_u32(counts[1] + i);
        uint32x4_t c2 = vld1q_u32(counts[2] + i);
        uint32x4_t c3 = vld1q_u32(counts[3] + i);
        vst1q_u32(total_counts + i, vaddq_u32(vaddq_u32(c0, c1), vaddq_u32(c2, c3)));
    }
}

void xx_entropy_histogram_neon(const void *data, size_t size, uint32_t total_counts[256])
{
    const uint8_t *p = (const uint8_t *)data;

    if (!total_counts) return;
    if (!p || size == 0U) {
        xx_rt_memset(total_counts, 0, 256U * sizeof(*total_counts));
        return;
    }
    if (size >= XX_ENTROPY_NEON_LARGE_MIN_SIZE) {
        xx_entropy_histogram_neon_large(p, size, total_counts);
    } else {
        xx_entropy_histogram_neon_small(p, size, total_counts);
    }
}

double xx_entropy_calculate_neon(const void *data, size_t size)
{
    uint32_t total_counts[256];
    if (!data || size == 0U) return 0.0;
    xx_entropy_histogram_neon(data, size, total_counts);
    return xx_entropy_neon_finish(total_counts, size);
}

#undef XX_ENTROPY_NEON_COUNT8
#undef XX_ENTROPY_NEON_LARGE_MIN_SIZE
#undef XX_ENTROPY_NEON_NOINLINE
#undef XX_ENTROPY_NEON_ALIGN64
#endif /* XX_ENTROPY_NEON */
