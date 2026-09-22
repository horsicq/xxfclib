/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_gashuff.h @brief GAS static-Huffman stream decoder. */

#ifndef XXFCLIB_ALGO_GASHUFF_H
#define XXFCLIB_ALGO_GASHUFF_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_gashuff_info_s {
    uint32_t uncompressed_size;
    uint16_t node_count;
    uint16_t root_index;
    size_t payload_bit_offset;
    size_t table_end_offset;
} xx_gashuff_info;

/** Parse and fully bounds-check a GAS Huffman header and node table. */
XXFC_API bool xx_gashuff_parse_memory(const uint8_t *input, size_t input_size,
                                      xx_gashuff_info *info);

/** Decode a GAS static-Huffman container into its exact declared size. */
XXFC_API bool xx_gashuff_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size, xx_gashuff_info *info);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_GASHUFF_H */
