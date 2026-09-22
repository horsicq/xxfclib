/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_oraclesqueeze.h @brief Oracle Squeeze entropy and RLE decoder. */

#ifndef XXFCLIB_ALGO_ORACLESQUEEZE_H
#define XXFCLIB_ALGO_ORACLESQUEEZE_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_oraclesqueeze_tree_info_s {
    uint16_t node_count;
    size_t bitstream_offset;
} xx_oraclesqueeze_tree_info;

/** Parse and bounds-check a Greenlaw Squeeze node table beginning at @p input. */
XXFC_API bool xx_oraclesqueeze_parse_tree(const uint8_t *input,
                                          size_t input_size,
                                          xx_oraclesqueeze_tree_info *info);

/** Decode the node table, LSB-first Huffman stream and 0x90 RLE stage.  The
 * caller supplies the exact uncompressed length. */
XXFC_API bool xx_oraclesqueeze_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size, uint16_t *checksum);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_ORACLESQUEEZE_H */
