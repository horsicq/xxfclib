/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lzv1.h @brief LZV1 phased-in LZW decoder. */

#ifndef XXFCLIB_ALGO_LZV1_H
#define XXFCLIB_ALGO_LZV1_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lzv1_header_s {
    uint16_t max_codes;
} xx_lzv1_header;

/** Parse the fixed 12-byte LZV1 container header. */
XXFC_API bool xx_lzv1_parse_header(const uint8_t *input, size_t input_size,
                                   xx_lzv1_header *header);

/** Decode a complete LZV1 container.  On success, @p output receives a buffer
 * allocated with xx_mem_alloc/xx_mem_realloc and must be released with
 * xx_mem_free; @p output_size is its exact decoded length. */
XXFC_API bool xx_lzv1_decompress_memory(const uint8_t *input,
                                        size_t input_size,
                                        uint8_t **output,
                                        size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZV1_H */
