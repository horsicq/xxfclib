/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_bytestats_sse2.c
 * @brief SSE2 byte statistics. Results equal the scalar reference exactly.
 */

#include "xx_bytestats_platform.h"

#ifdef XX_BYTESTATS_X86

XX_BYTESTATS_TARGET_SSE2
void xx_bytestats_histogram_sse2(const uint8_t *data, size_t size, uint64_t counts[256])
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
            __m128i first = _mm_set1_epi8((char)p[0]);
            __m128i e0 = _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)p), first);
            __m128i e1 = _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)(p + 16)), first);
            __m128i e2 = _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)(p + 32)), first);
            __m128i e3 = _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)(p + 48)), first);
            if (_mm_movemask_epi8(_mm_and_si128(_mm_and_si128(e0, e1), _mm_and_si128(e2, e3))) == 0xFFFF) {
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
}

XX_BYTESTATS_TARGET_SSE2
static uint64_t xx_bytestats_sse2_lanes(__m128i value)
{
    uint64_t lanes[2];
    _mm_storeu_si128((__m128i *)(void *)lanes, value);
    return lanes[0] + lanes[1];
}

XX_BYTESTATS_TARGET_SSE2
uint64_t xx_bytestats_count_sse2(const uint8_t *data, size_t size, uint8_t value)
{
    const __m128i needle = _mm_set1_epi8((char)value);
    const __m128i zero = _mm_setzero_si128();
    __m128i total = zero;
    size_t i = 0U;
    while (size - i >= 64U) {
        /* Byte counters subtract the all-ones compare mask; 255 rounds max. */
        size_t rounds = (size - i) / 64U;
        __m128i a0 = zero, a1 = zero, a2 = zero, a3 = zero;
        if (rounds > 255U) rounds = 255U;
        for (; rounds != 0U; --rounds, i += 64U) {
            const uint8_t *p = data + i;
            a0 = _mm_sub_epi8(a0, _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)p), needle));
            a1 = _mm_sub_epi8(a1, _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)(p + 16)), needle));
            a2 = _mm_sub_epi8(a2, _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)(p + 32)), needle));
            a3 = _mm_sub_epi8(a3, _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)(p + 48)), needle));
        }
        total = _mm_add_epi64(total, _mm_add_epi64(_mm_sad_epu8(a0, zero), _mm_sad_epu8(a1, zero)));
        total = _mm_add_epi64(total, _mm_add_epi64(_mm_sad_epu8(a2, zero), _mm_sad_epu8(a3, zero)));
    }
    return xx_bytestats_sse2_lanes(total) + xx_bytestats_count_scalar(data + i, size - i, value);
}

XX_BYTESTATS_TARGET_SSE2
uint64_t xx_bytestats_sum_sse2(const uint8_t *data, size_t size)
{
    const __m128i zero = _mm_setzero_si128();
    __m128i s0 = zero, s1 = zero;
    size_t i = 0U;
    for (; size - i >= 64U; i += 64U) {
        const uint8_t *p = data + i;
        s0 = _mm_add_epi64(s0, _mm_sad_epu8(_mm_loadu_si128((const __m128i *)(const void *)p), zero));
        s1 = _mm_add_epi64(s1, _mm_sad_epu8(_mm_loadu_si128((const __m128i *)(const void *)(p + 16)), zero));
        s0 = _mm_add_epi64(s0, _mm_sad_epu8(_mm_loadu_si128((const __m128i *)(const void *)(p + 32)), zero));
        s1 = _mm_add_epi64(s1, _mm_sad_epu8(_mm_loadu_si128((const __m128i *)(const void *)(p + 48)), zero));
    }
    return xx_bytestats_sse2_lanes(_mm_add_epi64(s0, s1)) + xx_bytestats_sum_scalar(data + i, size - i);
}

XX_BYTESTATS_TARGET_SSE2
void xx_bytestats_serial_sums_sse2(const uint8_t *data, size_t size, xx_bytestats_serial *out)
{
    const __m128i zero = _mm_setzero_si128();
    __m128i sums = zero, products = zero, squares = zero;
    xx_bytestats_serial tail;
    size_t i = 0U;
    /* Each round reads data[i .. i + 16], so it needs 17 bytes. */
    while (size - i >= 17U) {
        size_t rounds = (size - i - 1U) / 16U;
        __m128i products32 = zero, squares32 = zero;
        if (rounds > XX_BYTESTATS_SERIAL_ROUNDS) rounds = XX_BYTESTATS_SERIAL_ROUNDS;
        for (; rounds != 0U; --rounds, i += 16U) {
            __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(data + i));
            __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(data + i + 1U));
            __m128i a_lo = _mm_unpacklo_epi8(a, zero), a_hi = _mm_unpackhi_epi8(a, zero);
            __m128i b_lo = _mm_unpacklo_epi8(b, zero), b_hi = _mm_unpackhi_epi8(b, zero);
            products32 = _mm_add_epi32(products32, _mm_add_epi32(_mm_madd_epi16(a_lo, b_lo), _mm_madd_epi16(a_hi, b_hi)));
            squares32 = _mm_add_epi32(squares32, _mm_add_epi32(_mm_madd_epi16(a_lo, a_lo), _mm_madd_epi16(a_hi, a_hi)));
            sums = _mm_add_epi64(sums, _mm_sad_epu8(a, zero));
        }
        products = _mm_add_epi64(products, _mm_add_epi64(_mm_unpacklo_epi32(products32, zero), _mm_unpackhi_epi32(products32, zero)));
        squares = _mm_add_epi64(squares, _mm_add_epi64(_mm_unpacklo_epi32(squares32, zero), _mm_unpackhi_epi32(squares32, zero)));
    }
    /* The tail starts at i; every pair (k, k + 1) with k < i is already counted. */
    xx_bytestats_serial_sums_scalar(data + i, size - i, &tail);
    out->byte_sum = xx_bytestats_sse2_lanes(sums) + tail.byte_sum;
    out->square_sum = xx_bytestats_sse2_lanes(squares) + tail.square_sum;
    out->adjacent_sum = xx_bytestats_sse2_lanes(products) + tail.adjacent_sum;
}

#else
typedef int xx_bytestats_sse2_unused;
#endif /* XX_BYTESTATS_X86 */
