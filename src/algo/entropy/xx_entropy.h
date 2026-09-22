/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_ENTROPY_H
#define XX_ENTROPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

bool xx_reverse_bits_init(xx_reverse_bits *bits, const void *source,
                          size_t source_size);
bool xx_reverse_bits_read(xx_reverse_bits *bits, unsigned count,
                          uint32_t *value);
bool xx_reverse_bits_peek_padded(const xx_reverse_bits *bits, unsigned count,
                                 uint32_t *value);
bool xx_reverse_bits_skip(xx_reverse_bits *bits, unsigned count);

bool xx_fse_read_table(const uint8_t *source, size_t source_size,
                       unsigned maximum_symbol, unsigned maximum_table_log,
                       xx_fse_table *table, size_t *out_consumed);
bool xx_fse_build_table(const int16_t *normalized, unsigned maximum_symbol,
                        unsigned table_log, xx_fse_table *table);
bool xx_fse_decompress(const uint8_t *source, size_t source_size,
                       const xx_fse_table *table, uint8_t *destination,
                       size_t destination_capacity, size_t *out_written);

/** Reads a Zstandard-compatible Huffman description and replaces table. */
bool xx_huf_read_table(const uint8_t *source, size_t source_size,
                       xx_huf_table *table, size_t *out_consumed);

/** Decodes one reverse-bit Huffman stream with a previously read table. */
bool xx_huf_decode_1stream(const uint8_t *source, size_t source_size,
                           const xx_huf_table *table,
                           uint8_t *destination, size_t destination_size);

/** Decodes the four-stream Huffman payload used by Zstandard and Lizard. */
bool xx_huf_decode_4streams(const uint8_t *source, size_t source_size,
                            const xx_huf_table *table,
                            uint8_t *destination, size_t destination_size);

/** Decodes the self-describing four-stream Huffman format used by Lizard. */
bool xx_huf_decompress(const uint8_t *source, size_t source_size,
                       uint8_t *destination, size_t destination_size);

#endif /* XX_ENTROPY_H */
