/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_bytestats.h
 * @brief Exact integer byte statistics over a buffer.
 *
 * Histograms, single-byte counts, byte sums, the integer sums behind a serial
 * correlation coefficient and the circle-hit count of the Monte Carlo pi test.
 * Every result is an exact integer, so callers can rebuild floating-point
 * statistics in any required operation order. SSE2/AVX2 backends are chosen
 * at run time; the portable scalar code is the reference they must equal.
 */

#ifndef XX_BYTESTATS_H
#define XX_BYTESTATS_H

#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Backend selectors for the *_backend test entry points. */
#define XX_BYTESTATS_BACKEND_SCALAR 0
#define XX_BYTESTATS_BACKEND_SSE2 1
#define XX_BYTESTATS_BACKEND_AVX2 2

/** Integer sums of a serial correlation over data[0..size). */
typedef struct xx_bytestats_serial {
    uint64_t byte_sum;     /**< sum of data[i] */
    uint64_t square_sum;   /**< sum of data[i] * data[i] */
    uint64_t adjacent_sum; /**< sum of data[i] * data[i + 1] for i + 1 < size (no wrap-around term) */
} xx_bytestats_serial;

/** True when the backend is compiled in and enabled on this CPU. */
XXFC_API bool xx_bytestats_backend_available(int backend);

/** Replace counts with the number of occurrences of every byte value.
 * NULL data with size 0 yields all zeros. */
XXFC_API void xx_bytestats_histogram(const void *data, size_t size, uint64_t counts[256]);

/** Number of bytes equal to value. */
XXFC_API uint64_t xx_bytestats_count(const void *data, size_t size, uint8_t value);

/** Sum of all bytes as an unsigned 64-bit integer (exact below 2^56 bytes). */
XXFC_API uint64_t xx_bytestats_sum(const void *data, size_t size);

/** Byte sum, square sum and adjacent-product sum (exact below 2^47 bytes). */
XXFC_API void xx_bytestats_serial_sums(const void *data, size_t size, xx_bytestats_serial *out);

/** Split data into size / 6 big-endian 24-bit pairs (x = bytes 0..2, y = bytes 3..5)
 * and count the pairs with x * x + y * y < limit. Trailing bytes are ignored. */
XXFC_API uint64_t xx_bytestats_circle_hits(const void *data, size_t size, uint64_t limit);

/* Floating-point statistics rebuilt from the exact integer sums above.
 * Each function fixes one operation order; the orders reproduce YARA-X's
 * math module bit for bit, so never "simplify" them. */

/** Mean byte value from a histogram: sum(i * counts[i]) / total, accumulated
 * over i = 0..255 in ascending order. */
XXFC_API double xx_bytestats_mean_counts(const uint64_t counts[256], uint64_t total);

/** Mean absolute deviation from mean over a histogram:
 * sum(|i - mean| * counts[i]) / total, accumulated over i = 0..255. */
XXFC_API double xx_bytestats_deviation_counts(const uint64_t counts[256], uint64_t total, double mean);

/** Most frequent byte value; the smallest index wins ties, as YARA-X does. */
XXFC_API size_t xx_bytestats_mode_counts(const uint64_t counts[256]);

/** Serial correlation coefficient of data[0..size), in YARA-X operation order.
 * Returns -100000.0 for empty input and whenever the coefficient is 0/0. */
XXFC_API double xx_bytestats_serial_correlation(const void *data, size_t size);

/** Monte Carlo pi deviation: |4 * inside/chunks - pi| / pi over the 6-byte
 * points of data. Fails (leaving *out untouched) when size < 6, i.e. when
 * no complete point exists. data must point to size readable bytes. */
XXFC_API bool xx_bytestats_monte_carlo_pi(const void *data, size_t size, double *out);

/* Explicit-backend entry points for differential tests. They return false and
 * leave the outputs untouched when the backend is not available. */
XXFC_API bool xx_bytestats_histogram_backend(int backend, const void *data, size_t size, uint64_t counts[256]);
XXFC_API bool xx_bytestats_count_backend(int backend, const void *data, size_t size, uint8_t value, uint64_t *count);
XXFC_API bool xx_bytestats_sum_backend(int backend, const void *data, size_t size, uint64_t *sum);
XXFC_API bool xx_bytestats_serial_sums_backend(int backend, const void *data, size_t size, xx_bytestats_serial *out);
XXFC_API bool xx_bytestats_circle_hits_backend(int backend, const void *data, size_t size, uint64_t limit, uint64_t *hits);

#ifdef __cplusplus
}
#endif

#endif /* XX_BYTESTATS_H */
