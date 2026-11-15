/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_entropy.h
 * @brief Shannon entropy calculation and entropy coding primitives (FSE, Huffman).
 */

#ifndef XX_ENTROPY_H
#define XX_ENTROPY_H

#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------- Shannon Entropy */

/**
 * @brief Calculate Shannon entropy in bits per byte [0.0 .. 8.0].
 *
 * Uses ILP multi-histogram table accumulation to maximize memory bandwidth
 * and reduce indexed counter dependency stalls.
 *
 * @param data Pointer to input data buffer.
 * @param size Size of data in bytes.
 * @return Shannon entropy value from 0.0 to 8.0 (bits/byte).
 */
XXFC_API double xx_entropy_calculate(const void *data, size_t size);

/**
 * @brief Alias for xx_entropy_calculate.
 */
XXFC_API double xx_entropy(const void *data, size_t size);

/**
 * @brief YARA-X-form entropy from a byte histogram: -Σ p·log2(p).
 *
 * Sums -p·log2(p) over byte values 0..255 in ascending order, skipping zero
 * counts, with p = counts[i] / total. This is the exact evaluation order of
 * YARA-X's math::entropy (and of the classic -p·log2(p) definition), which
 * xx_entropy_calculate's (ln N - Σ c·ln c / N)/ln 2 form is NOT bit-identical
 * to. Returns 0.0 for total == 0.
 */
XXFC_API double xx_entropy_log2_counts(const uint64_t counts[256], uint64_t total);

/**
 * @brief YARA-X-form entropy of a buffer: -Σ p·log2(p).
 *
 * Builds the histogram with xx_bytestats_histogram and evaluates
 * xx_entropy_log2_counts on it. data may be NULL when size is 0.
 */
XXFC_API double xx_entropy_log2(const void *data, size_t size);

struct xx_io_device;

/** Calculate Shannon entropy over a device range, in bits per byte.
 * Negative size means the remaining bytes; oversized ranges stop at EOF.
 * Invalid/unknown-size devices, invalid offsets, empty ranges, allocation
 * failures, and read failures return 0.0. The device is borrowed. Buffered
 * inputs keep their logical cursor; ordinary inputs use a temporary buffer
 * whose reads reposition the source. */
XXFC_API double xx_entropy_calculate_device(struct xx_io_device *dev, int64_t offset, int64_t size);

/* --------------------------------------------- Entropy Coding Primitives  */

#define XX_FSE_MAX_TABLE_LOG 12U
#define XX_FSE_MAX_SYMBOL_VALUE 255U

typedef struct xx_reverse_bits {
    const uint8_t *data;
    size_t bit_position;
} xx_reverse_bits;

typedef struct xx_fse_entry {
    uint16_t new_state;
    uint8_t symbol;
    uint8_t bit_count;
} xx_fse_entry;

typedef struct xx_fse_table {
    unsigned table_log;
    unsigned table_size;
    xx_fse_entry entries[1U << XX_FSE_MAX_TABLE_LOG];
} xx_fse_table;

typedef struct xx_huf_entry {
    uint8_t symbol;
    uint8_t bit_count;
} xx_huf_entry;

typedef struct xx_huf_table {
    unsigned table_log;
    xx_huf_entry entries[1U << 12];
} xx_huf_table;

XXFC_API bool xx_reverse_bits_init(xx_reverse_bits *bits, const void *source, size_t source_size);
XXFC_API bool xx_reverse_bits_read(xx_reverse_bits *bits, unsigned count, uint32_t *value);
XXFC_API bool xx_reverse_bits_peek_padded(const xx_reverse_bits *bits, unsigned count, uint32_t *value);
XXFC_API bool xx_reverse_bits_skip(xx_reverse_bits *bits, unsigned count);

XXFC_API bool xx_fse_read_table(const uint8_t *source, size_t source_size, unsigned maximum_symbol, unsigned maximum_table_log, xx_fse_table *table,
                                size_t *out_consumed);
XXFC_API bool xx_fse_build_table(const int16_t *normalized, unsigned maximum_symbol, unsigned table_log, xx_fse_table *table);
XXFC_API bool xx_fse_decompress(const uint8_t *source, size_t source_size, const xx_fse_table *table, uint8_t *destination, size_t destination_capacity,
                                size_t *out_written);

/** Reads a Zstandard-compatible Huffman description and replaces table. */
XXFC_API bool xx_huf_read_table(const uint8_t *source, size_t source_size, xx_huf_table *table, size_t *out_consumed);

/** Decodes one reverse-bit Huffman stream with a previously read table. */
XXFC_API bool xx_huf_decode_1stream(const uint8_t *source, size_t source_size, const xx_huf_table *table, uint8_t *destination, size_t destination_size);

/** Decodes the four-stream Huffman payload used by Zstandard and Lizard. */
XXFC_API bool xx_huf_decode_4streams(const uint8_t *source, size_t source_size, const xx_huf_table *table, uint8_t *destination, size_t destination_size);

/** Decodes the self-describing four-stream Huffman format used by Lizard. */
XXFC_API bool xx_huf_decompress(const uint8_t *source, size_t source_size, uint8_t *destination, size_t destination_size);

#ifdef __cplusplus
}
#endif

#endif /* XX_ENTROPY_H */
