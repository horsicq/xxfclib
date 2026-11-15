/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_BYTESTATS_PLATFORM_H
#define XX_BYTESTATS_PLATFORM_H

#include "xxfclib/algo/bytestats/xx_bytestats.h"

#include <string.h>

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)) && !defined(_M_ARM64EC)
#define XX_BYTESTATS_X86 1
#include <immintrin.h>
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_BYTESTATS_X86)
#define XX_BYTESTATS_TARGET_SSE2 __attribute__((target("sse2")))
#define XX_BYTESTATS_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_BYTESTATS_TARGET_SSE2
#define XX_BYTESTATS_TARGET_AVX2
#endif

/* Scalar helpers shared with the SIMD backends (xx_bytestats.c). */
void xx_bytestats_histogram_scalar(const uint8_t *data, size_t size, uint64_t counts[256]);
uint64_t xx_bytestats_count_scalar(const uint8_t *data, size_t size, uint8_t value);
uint64_t xx_bytestats_sum_scalar(const uint8_t *data, size_t size);
void xx_bytestats_serial_sums_scalar(const uint8_t *data, size_t size, xx_bytestats_serial *out);
uint64_t xx_bytestats_circle_hits_scalar(const uint8_t *data, size_t size, uint64_t limit);

/* Multi-table counting into 32-bit sub-histograms; size <= XX_BYTESTATS_TABLE_CHUNK
 * keeps every sub-table counter below 2^32. */
#define XX_BYTESTATS_TABLE_CHUNK ((size_t)1 << 30)
void xx_bytestats_tables_add(const uint8_t *data, size_t size, uint32_t tables[4][256]);

/* Count 16 bytes at p into four sub-tables (four 32-bit loads). The byte
 * order inside a word only changes which sub-table a byte lands in. */
#define XX_BYTESTATS_COUNT_WORD(word, tables)    \
    do {                                         \
        ++(tables)[0][(word) & 0xFFU];           \
        ++(tables)[1][((word) >> 8) & 0xFFU];    \
        ++(tables)[2][((word) >> 16) & 0xFFU];   \
        ++(tables)[3][(word) >> 24];             \
    } while (0)
#define XX_BYTESTATS_COUNT16(p, tables)          \
    do {                                         \
        uint32_t xx_bs_w0, xx_bs_w1, xx_bs_w2, xx_bs_w3; \
        memcpy(&xx_bs_w0, (p), 4U);              \
        memcpy(&xx_bs_w1, (p) + 4U, 4U);         \
        memcpy(&xx_bs_w2, (p) + 8U, 4U);         \
        memcpy(&xx_bs_w3, (p) + 12U, 4U);        \
        XX_BYTESTATS_COUNT_WORD(xx_bs_w0, tables); \
        XX_BYTESTATS_COUNT_WORD(xx_bs_w1, tables); \
        XX_BYTESTATS_COUNT_WORD(xx_bs_w2, tables); \
        XX_BYTESTATS_COUNT_WORD(xx_bs_w3, tables); \
    } while (0)

/* Per-lane bound of the 32-bit serial-sum accumulators: each SIMD iteration
 * adds at most 4 * 255 * 255 = 260100 to a lane, 8192 * 260100 < 2^32. */
#define XX_BYTESTATS_SERIAL_ROUNDS ((size_t)8192U)

/* Histograms below this size use the direct single-table loop. */
#define XX_BYTESTATS_SMALL_HISTOGRAM ((size_t)512U)

#ifdef XX_BYTESTATS_X86
void xx_bytestats_histogram_sse2(const uint8_t *data, size_t size, uint64_t counts[256]);
uint64_t xx_bytestats_count_sse2(const uint8_t *data, size_t size, uint8_t value);
uint64_t xx_bytestats_sum_sse2(const uint8_t *data, size_t size);
void xx_bytestats_serial_sums_sse2(const uint8_t *data, size_t size, xx_bytestats_serial *out);

void xx_bytestats_histogram_avx2(const uint8_t *data, size_t size, uint64_t counts[256]);
uint64_t xx_bytestats_count_avx2(const uint8_t *data, size_t size, uint8_t value);
uint64_t xx_bytestats_sum_avx2(const uint8_t *data, size_t size);
void xx_bytestats_serial_sums_avx2(const uint8_t *data, size_t size, xx_bytestats_serial *out);
uint64_t xx_bytestats_circle_hits_avx2(const uint8_t *data, size_t size, uint64_t limit);
#endif

#endif /* XX_BYTESTATS_PLATFORM_H */
