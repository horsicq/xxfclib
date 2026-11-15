/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_bytestats_avx2.c
 * @brief AVX2 byte statistics. Results equal the scalar reference exactly.
 */

#include "xx_bytestats_platform.h"

#ifdef XX_BYTESTATS_X86

XX_BYTESTATS_TARGET_AVX2
void xx_bytestats_histogram_avx2(const uint8_t *data, size_t size, uint64_t counts[256])
{
    if (size < XX_BYTESTATS_SMALL_HISTOGRAM) {
        xx_bytestats_histogram_scalar(data, size, counts);
        return;
    }
    memset(counts, 0, 256U * sizeof(*counts));
    while (size != 0U) {
        uint32_t tables[4][256];
        size_t part = size < XX_BYTESTATS_TABLE_CHUNK ? size : XX_BYTESTATS_TABLE_CHUNK;
        size_t i = 0U;
        unsigned value;
        memset(tables, 0, sizeof(tables));
        /* A 64-byte block of one value (zero padding, fill runs) is added at once. */
        for (; part - i >= 64U; i += 64U) {
            const uint8_t *p = data + i;
            __m256i first = _mm256_set1_epi8((char)p[0]);
            __m256i e0 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(const void *)p), first);
            __m256i e1 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(const void *)(p + 32)), first);
            if (_mm256_movemask_epi8(_mm256_and_si256(e0, e1)) == -1) {
                tables[0][p[0]] += 64U;
                continue;
            }
            XX_BYTESTATS_COUNT16(p, tables);
            XX_BYTESTATS_COUNT16(p + 16, tables);
            XX_BYTESTATS_COUNT16(p + 32, tables);
            XX_BYTESTATS_COUNT16(p + 48, tables);
        }
        xx_bytestats_tables_add(data + i, part - i, tables);
        for (value = 0U; value < 256U; ++value)
            counts[value] += (uint64_t)tables[0][value] + tables[1][value] + tables[2][value] + tables[3][value];
        data += part;
        size -= part;
    }
    _mm256_zeroupper();
}

XX_BYTESTATS_TARGET_AVX2
static uint64_t xx_bytestats_avx2_lanes(__m256i value)
{
    __m128i half = _mm_add_epi64(_mm256_castsi256_si128(value), _mm256_extracti128_si256(value, 1));
    uint64_t lanes[2];
    _mm_storeu_si128((__m128i *)(void *)lanes, half);
    return lanes[0] + lanes[1];
}

XX_BYTESTATS_TARGET_AVX2
uint64_t xx_bytestats_count_avx2(const uint8_t *data, size_t size, uint8_t value)
{
    const __m256i needle = _mm256_set1_epi8((char)value);
    const __m256i zero = _mm256_setzero_si256();
    __m256i total = zero;
    uint64_t count;
    size_t i = 0U;
    while (size - i >= 128U) {
        /* Byte counters subtract the all-ones compare mask; 255 rounds max. */
        size_t rounds = (size - i) / 128U;
        __m256i a0 = zero, a1 = zero, a2 = zero, a3 = zero;
        if (rounds > 255U) rounds = 255U;
        for (; rounds != 0U; --rounds, i += 128U) {
            const uint8_t *p = data + i;
            a0 = _mm256_sub_epi8(a0, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(const void *)p), needle));
            a1 = _mm256_sub_epi8(a1, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(const void *)(p + 32)), needle));
            a2 = _mm256_sub_epi8(a2, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(const void *)(p + 64)), needle));
            a3 = _mm256_sub_epi8(a3, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(const void *)(p + 96)), needle));
        }
        total = _mm256_add_epi64(total, _mm256_add_epi64(_mm256_sad_epu8(a0, zero), _mm256_sad_epu8(a1, zero)));
        total = _mm256_add_epi64(total, _mm256_add_epi64(_mm256_sad_epu8(a2, zero), _mm256_sad_epu8(a3, zero)));
    }
    count = xx_bytestats_avx2_lanes(total);
    _mm256_zeroupper();
    return count + xx_bytestats_count_scalar(data + i, size - i, value);
}

XX_BYTESTATS_TARGET_AVX2
uint64_t xx_bytestats_sum_avx2(const uint8_t *data, size_t size)
{
    const __m256i zero = _mm256_setzero_si256();
    __m256i s0 = zero, s1 = zero;
    uint64_t sum;
    size_t i = 0U;
    for (; size - i >= 128U; i += 128U) {
        const uint8_t *p = data + i;
        s0 = _mm256_add_epi64(s0, _mm256_sad_epu8(_mm256_loadu_si256((const __m256i *)(const void *)p), zero));
        s1 = _mm256_add_epi64(s1, _mm256_sad_epu8(_mm256_loadu_si256((const __m256i *)(const void *)(p + 32)), zero));
        s0 = _mm256_add_epi64(s0, _mm256_sad_epu8(_mm256_loadu_si256((const __m256i *)(const void *)(p + 64)), zero));
        s1 = _mm256_add_epi64(s1, _mm256_sad_epu8(_mm256_loadu_si256((const __m256i *)(const void *)(p + 96)), zero));
    }
    sum = xx_bytestats_avx2_lanes(_mm256_add_epi64(s0, s1));
    _mm256_zeroupper();
    return sum + xx_bytestats_sum_scalar(data + i, size - i);
}

XX_BYTESTATS_TARGET_AVX2
void xx_bytestats_serial_sums_avx2(const uint8_t *data, size_t size, xx_bytestats_serial *out)
{
    const __m256i zero = _mm256_setzero_si256();
    __m256i sums = zero, products = zero, squares = zero;
    xx_bytestats_serial tail;
    size_t i = 0U;
    /* Each round reads data[i .. i + 32], so it needs 33 bytes. Unpacking
     * inside 128-bit lanes keeps a[k] and b[k] = data[i + k + 1] paired. */
    while (size - i >= 33U) {
        size_t rounds = (size - i - 1U) / 32U;
        __m256i products32 = zero, squares32 = zero;
        if (rounds > XX_BYTESTATS_SERIAL_ROUNDS) rounds = XX_BYTESTATS_SERIAL_ROUNDS;
        for (; rounds != 0U; --rounds, i += 32U) {
            __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(data + i));
            __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(data + i + 1U));
            __m256i a_lo = _mm256_unpacklo_epi8(a, zero), a_hi = _mm256_unpackhi_epi8(a, zero);
            __m256i b_lo = _mm256_unpacklo_epi8(b, zero), b_hi = _mm256_unpackhi_epi8(b, zero);
            products32 = _mm256_add_epi32(products32,
                                          _mm256_add_epi32(_mm256_madd_epi16(a_lo, b_lo), _mm256_madd_epi16(a_hi, b_hi)));
            squares32 = _mm256_add_epi32(squares32,
                                         _mm256_add_epi32(_mm256_madd_epi16(a_lo, a_lo), _mm256_madd_epi16(a_hi, a_hi)));
            sums = _mm256_add_epi64(sums, _mm256_sad_epu8(a, zero));
        }
        products = _mm256_add_epi64(products, _mm256_add_epi64(_mm256_unpacklo_epi32(products32, zero),
                                                               _mm256_unpackhi_epi32(products32, zero)));
        squares = _mm256_add_epi64(squares, _mm256_add_epi64(_mm256_unpacklo_epi32(squares32, zero),
                                                             _mm256_unpackhi_epi32(squares32, zero)));
    }
    out->byte_sum = xx_bytestats_avx2_lanes(sums);
    out->square_sum = xx_bytestats_avx2_lanes(squares);
    out->adjacent_sum = xx_bytestats_avx2_lanes(products);
    _mm256_zeroupper();
    /* The tail starts at i; every pair (k, k + 1) with k < i is already counted. */
    xx_bytestats_serial_sums_scalar(data + i, size - i, &tail);
    out->byte_sum += tail.byte_sum;
    out->square_sum += tail.square_sum;
    out->adjacent_sum += tail.adjacent_sum;
}

XX_BYTESTATS_TARGET_AVX2
uint64_t xx_bytestats_circle_hits_avx2(const uint8_t *data, size_t size, uint64_t limit)
{
    /* Per 128-bit lane: two 6-byte tuples become 64-bit x (bytes 0..2) and
     * y (bytes 3..5) values, big-endian like the scalar reference. */
    const __m256i x_order = _mm256_setr_epi8(2, 1, 0, -128, -128, -128, -128, -128, 8, 7, 6, -128, -128, -128, -128, -128,
                                             2, 1, 0, -128, -128, -128, -128, -128, 8, 7, 6, -128, -128, -128, -128, -128);
    const __m256i y_order = _mm256_setr_epi8(5, 4, 3, -128, -128, -128, -128, -128, 11, 10, 9, -128, -128, -128, -128, -128,
                                             5, 4, 3, -128, -128, -128, -128, -128, 11, 10, 9, -128, -128, -128, -128, -128);
    __m256i bound, hits = _mm256_setzero_si256();
    size_t chunks = size / 6U;
    size_t chunk = 0U;
    uint64_t total;
    /* The signed 64-bit compare needs limit <= INT64_MAX; x*x + y*y < 2^49. */
    if (limit > (uint64_t)INT64_MAX) return xx_bytestats_circle_hits_scalar(data, size, limit);
    bound = _mm256_set1_epi64x((long long)limit);
    /* Four tuples per round read 28 bytes (two 16-byte loads at +0 and +12). */
    while (chunk + 4U <= chunks && size - chunk * 6U >= 28U) {
        const uint8_t *p = data + chunk * 6U;
        __m256i v = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_loadu_si128((const __m128i *)(const void *)p)),
                                            _mm_loadu_si128((const __m128i *)(const void *)(p + 12)), 1);
        __m256i x = _mm256_shuffle_epi8(v, x_order);
        __m256i y = _mm256_shuffle_epi8(v, y_order);
        __m256i distance = _mm256_add_epi64(_mm256_mul_epu32(x, x), _mm256_mul_epu32(y, y));
        hits = _mm256_sub_epi64(hits, _mm256_cmpgt_epi64(bound, distance));
        chunk += 4U;
    }
    total = xx_bytestats_avx2_lanes(hits);
    _mm256_zeroupper();
    return total + xx_bytestats_circle_hits_scalar(data + chunk * 6U, size - chunk * 6U, limit);
}

#else
typedef int xx_bytestats_avx2_unused;
#endif /* XX_BYTESTATS_X86 */
