/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ash0.h @brief Nintendo ASH0 dual-Huffman decoder. */

#ifndef XXFCLIB_ALGO_ASH0_H
#define XXFCLIB_ALGO_ASH0_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ash0_header_s {
    uint32_t uncompressed_size;
    uint32_t distance_offset;
    uint8_t size_word_top_byte;
} xx_ash0_header;

/** Validate an ASH0 header against the complete packed member. */
XXFC_API bool xx_ash0_parse_header(const uint8_t *input, size_t input_size,
                                   xx_ash0_header *header);

/** Decode an ASH0 member to its exactly declared output size.  ASH0 does not
 * record whether the distance tree uses 11 or 15-bit leaves; the decoder
 * validates both forms and returns the selected width through @p distance_bits
 * when non-NULL. */
XXFC_API bool xx_ash0_decompress_memory(const uint8_t *input,
                                        size_t input_size,
                                        uint8_t *output,
                                        size_t output_size,
                                        unsigned *distance_bits);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_ASH0_H */
