/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_bytestats.c
 * @brief Portable reference code and run-time dispatch for exact byte statistics.
 */

#include "platforms/xx_bytestats_platform.h"
#include "xxfclib/global/xx_global.h"

#include <math.h>
#include <string.h>

void xx_bytestats_tables_add(const uint8_t *data, size_t size, uint32_t tables[4][256])
{
    size_t i = 0U;
    /* Four sub-tables break the store-to-load chain of runs of one byte value;
     * 32-bit loads cut the load-port pressure of byte loads. */
    for (; size - i >= 16U; i += 16U) XX_BYTESTATS_COUNT16(data + i, tables);
    for (; i < size; ++i) ++tables[i & 3U][data[i]];
}

void xx_bytestats_histogram_scalar(const uint8_t *data, size_t size, uint64_t counts[256])
{
    memset(counts, 0, 256U * sizeof(*counts));
    if (size < XX_BYTESTATS_SMALL_HISTOGRAM) {
        size_t i;
        for (i = 0U; i < size; ++i) ++counts[data[i]];
        return;
    }
    while (size != 0U) {
        uint32_t tables[4][256];
        size_t part = size < XX_BYTESTATS_TABLE_CHUNK ? size : XX_BYTESTATS_TABLE_CHUNK;
        unsigned value;
        memset(tables, 0, sizeof(tables));
        xx_bytestats_tables_add(data, part, tables);
        for (value = 0U; value < 256U; ++value)
            counts[value] += (uint64_t)tables[0][value] + tables[1][value] + tables[2][value] + tables[3][value];
        data += part;
        size -= part;
    }
}

uint64_t xx_bytestats_count_scalar(const uint8_t *data, size_t size, uint8_t value)
{
    uint64_t count = 0U;
    size_t i;
    for (i = 0U; i < size; ++i) count += data[i] == value;
    return count;
}

uint64_t xx_bytestats_sum_scalar(const uint8_t *data, size_t size)
{
    uint64_t sum = 0U;
    size_t i = 0U;
    /* SWAR: add byte lanes pairwise inside 64-bit words. */
    for (; size - i >= 8U; i += 8U) {
        uint64_t word, pairs, quads;
        memcpy(&word, data + i, 8U);
        pairs = (word & UINT64_C(0x00FF00FF00FF00FF)) + ((word >> 8) & UINT64_C(0x00FF00FF00FF00FF));
        quads = (pairs & UINT64_C(0x0000FFFF0000FFFF)) + ((pairs >> 16) & UINT64_C(0x0000FFFF0000FFFF));
        sum += (quads & UINT64_C(0xFFFFFFFF)) + (quads >> 32);
    }
    for (; i < size; ++i) sum += data[i];
    return sum;
}

void xx_bytestats_serial_sums_scalar(const uint8_t *data, size_t size, xx_bytestats_serial *out)
{
    uint64_t byte_sum = 0U, square_sum = 0U, adjacent_sum = 0U;
    size_t i;
    for (i = 0U; i < size; ++i) {
        uint64_t byte = data[i];
        byte_sum += byte;
        square_sum += byte * byte;
        if (i + 1U < size) adjacent_sum += byte * data[i + 1U];
    }
    out->byte_sum = byte_sum;
    out->square_sum = square_sum;
    out->adjacent_sum = adjacent_sum;
}

uint64_t xx_bytestats_circle_hits_scalar(const uint8_t *data, size_t size, uint64_t limit)
{
    uint64_t hits = 0U;
    size_t chunks = size / 6U;
    size_t chunk;
    for (chunk = 0U; chunk < chunks; ++chunk) {
        const uint8_t *bytes = data + chunk * 6U;
        uint64_t x = ((uint64_t)bytes[0] << 16) | ((uint64_t)bytes[1] << 8) | bytes[2];
        uint64_t y = ((uint64_t)bytes[3] << 16) | ((uint64_t)bytes[4] << 8) | bytes[5];
        hits += x * x + y * y < limit;
    }
    return hits;
}

/* ------------------------------------------------------------- dispatch */

static int xx_bytestats_best_backend(void)
{
#ifdef XX_BYTESTATS_X86
    if (xx_is_avx2_enabled()) return XX_BYTESTATS_BACKEND_AVX2;
    if (xx_is_sse2_enabled()) return XX_BYTESTATS_BACKEND_SSE2;
#endif
    return XX_BYTESTATS_BACKEND_SCALAR;
}

bool xx_bytestats_backend_available(int backend)
{
    if (backend == XX_BYTESTATS_BACKEND_SCALAR) return true;
#ifdef XX_BYTESTATS_X86
    if (backend == XX_BYTESTATS_BACKEND_SSE2) return xx_is_sse2_enabled();
    if (backend == XX_BYTESTATS_BACKEND_AVX2) return xx_is_avx2_enabled();
#endif
    return false;
}

bool xx_bytestats_histogram_backend(int backend, const void *data, size_t size, uint64_t counts[256])
{
    const uint8_t *bytes = (const uint8_t *)data;
    if (!counts || (!data && size) || !xx_bytestats_backend_available(backend)) return false;
#ifdef XX_BYTESTATS_X86
    if (backend == XX_BYTESTATS_BACKEND_AVX2) {
        xx_bytestats_histogram_avx2(bytes, size, counts);
        return true;
    }
    if (backend == XX_BYTESTATS_BACKEND_SSE2) {
        xx_bytestats_histogram_sse2(bytes, size, counts);
        return true;
    }
#endif
    xx_bytestats_histogram_scalar(bytes, size, counts);
    return true;
}

bool xx_bytestats_count_backend(int backend, const void *data, size_t size, uint8_t value, uint64_t *count)
{
    const uint8_t *bytes = (const uint8_t *)data;
    if (!count || (!data && size) || !xx_bytestats_backend_available(backend)) return false;
#ifdef XX_BYTESTATS_X86
    if (backend == XX_BYTESTATS_BACKEND_AVX2) {
        *count = xx_bytestats_count_avx2(bytes, size, value);
        return true;
    }
    if (backend == XX_BYTESTATS_BACKEND_SSE2) {
        *count = xx_bytestats_count_sse2(bytes, size, value);
        return true;
    }
#endif
    *count = xx_bytestats_count_scalar(bytes, size, value);
    return true;
}

bool xx_bytestats_sum_backend(int backend, const void *data, size_t size, uint64_t *sum)
{
    const uint8_t *bytes = (const uint8_t *)data;
    if (!sum || (!data && size) || !xx_bytestats_backend_available(backend)) return false;
#ifdef XX_BYTESTATS_X86
    if (backend == XX_BYTESTATS_BACKEND_AVX2) {
        *sum = xx_bytestats_sum_avx2(bytes, size);
        return true;
    }
    if (backend == XX_BYTESTATS_BACKEND_SSE2) {
        *sum = xx_bytestats_sum_sse2(bytes, size);
        return true;
    }
#endif
    *sum = xx_bytestats_sum_scalar(bytes, size);
    return true;
}

bool xx_bytestats_serial_sums_backend(int backend, const void *data, size_t size, xx_bytestats_serial *out)
{
    const uint8_t *bytes = (const uint8_t *)data;
    if (!out || (!data && size) || !xx_bytestats_backend_available(backend)) return false;
#ifdef XX_BYTESTATS_X86
    if (backend == XX_BYTESTATS_BACKEND_AVX2) {
        xx_bytestats_serial_sums_avx2(bytes, size, out);
        return true;
    }
    if (backend == XX_BYTESTATS_BACKEND_SSE2) {
        xx_bytestats_serial_sums_sse2(bytes, size, out);
        return true;
    }
#endif
    xx_bytestats_serial_sums_scalar(bytes, size, out);
    return true;
}

bool xx_bytestats_circle_hits_backend(int backend, const void *data, size_t size, uint64_t limit, uint64_t *hits)
{
    const uint8_t *bytes = (const uint8_t *)data;
    if (!hits || (!data && size) || !xx_bytestats_backend_available(backend)) return false;
#ifdef XX_BYTESTATS_X86
    if (backend == XX_BYTESTATS_BACKEND_AVX2) {
        *hits = xx_bytestats_circle_hits_avx2(bytes, size, limit);
        return true;
    }
#endif
    /* SSE2 lacks a 64-bit compare; it shares the scalar loop. */
    *hits = xx_bytestats_circle_hits_scalar(bytes, size, limit);
    return true;
}

void xx_bytestats_histogram(const void *data, size_t size, uint64_t counts[256])
{
    if (!counts) return;
    if (!data || size == 0U) {
        memset(counts, 0, 256U * sizeof(*counts));
        return;
    }
    (void)xx_bytestats_histogram_backend(xx_bytestats_best_backend(), data, size, counts);
}

uint64_t xx_bytestats_count(const void *data, size_t size, uint8_t value)
{
    uint64_t count = 0U;
    if (!data || size == 0U) return 0U;
    (void)xx_bytestats_count_backend(xx_bytestats_best_backend(), data, size, value, &count);
    return count;
}

uint64_t xx_bytestats_sum(const void *data, size_t size)
{
    uint64_t sum = 0U;
    if (!data || size == 0U) return 0U;
    (void)xx_bytestats_sum_backend(xx_bytestats_best_backend(), data, size, &sum);
    return sum;
}

void xx_bytestats_serial_sums(const void *data, size_t size, xx_bytestats_serial *out)
{
    if (!out) return;
    if (!data || size == 0U) {
        memset(out, 0, sizeof(*out));
        return;
    }
    (void)xx_bytestats_serial_sums_backend(xx_bytestats_best_backend(), data, size, out);
}

uint64_t xx_bytestats_circle_hits(const void *data, size_t size, uint64_t limit)
{
    uint64_t hits = 0U;
    if (!data || size < 6U) return 0U;
    (void)xx_bytestats_circle_hits_backend(xx_bytestats_best_backend(), data, size, limit, &hits);
    return hits;
}

double xx_bytestats_mean_counts(const uint64_t counts[256], uint64_t total)
{
    double sum = 0.0;
    if (!counts || !total) return 0.0;
    for (size_t i = 0; i < 256; ++i)
        sum += (double)i * (double)counts[i];
    return sum / (double)total;
}

double xx_bytestats_deviation_counts(const uint64_t counts[256], uint64_t total, double mean)
{
    double sum = 0.0;
    if (!counts || !total) return 0.0;
    for (size_t i = 0; i < 256; ++i)
        sum += fabs((double)i - mean) * (double)counts[i];
    return sum / (double)total;
}

size_t xx_bytestats_mode_counts(const uint64_t counts[256])
{
    size_t mode = 0;
    if (!counts) return 0;
    for (size_t i = 1; i < 256; ++i)
        if (counts[i] > counts[mode]) mode = i;
    return mode;
}

/* YARA-X accumulates the serial correlation sums in f64. Every partial sum
 * is an integer of at most length * 255 * 255, exact while that stays below
 * 2^53, so exact integer sums convert to the same doubles. */
#define XX_BYTESTATS_EXACT_SERIAL_LENGTH ((UINT64_C(1) << 53) / UINT64_C(65025))

double xx_bytestats_serial_correlation(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    double first, previous, product_sum = 0.0;
    double byte_sum, square_sum, count, numerator, denominator, result;
    if (size == 0) return -100000.0;
    if ((uint64_t)size <= XX_BYTESTATS_EXACT_SERIAL_LENGTH) {
        xx_bytestats_serial sums;
        xx_bytestats_serial_sums(bytes, size, &sums);
        product_sum = (double)(sums.adjacent_sum +
                               (uint64_t)bytes[0] * bytes[size - 1]);
        byte_sum = (double)sums.byte_sum;
        square_sum = (double)sums.square_sum;
    } else {
        first = (double)bytes[0];
        previous = first;
        byte_sum = first;
        square_sum = first * first;
        for (size_t i = 1; i < size; ++i) {
            double byte = (double)bytes[i];
            product_sum += previous * byte;
            byte_sum += byte;
            square_sum += byte * byte;
            previous = byte;
        }
        product_sum += first * previous;
    }
    count = (double)size;
    numerator = count * product_sum - byte_sum * byte_sum;
    denominator = count * square_sum - byte_sum * byte_sum;
    result = numerator / denominator;
    return isnan(result) ? -100000.0 : result;
}

bool xx_bytestats_monte_carlo_pi(const void *data, size_t size, double *out)
{
    const double pi = 3.14159265358979323846264338327950288;
    const double in_circle_limit = 281474943156225.0;
    size_t chunks = size / 6;
    uint64_t inside;
    if (!out) return false;
    /* YARA-X compares f64 values that are exact integers (x, y < 2^24, the
     * sum and the limit below 2^53), so the integer comparison is identical. */
    if (chunks == 0) return false;
    inside = xx_bytestats_circle_hits(data, size, (uint64_t)in_circle_limit);
    *out = fabs(4.0 * ((double)inside / (double)chunks) - pi) / pi;
    return true;
}
