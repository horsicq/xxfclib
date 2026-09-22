/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lzpis2.h @brief LZPIS2 chunked adaptive-Huffman decoder. */

#ifndef XXFCLIB_ALGO_LZPIS2_H
#define XXFCLIB_ALGO_LZPIS2_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lzpis2_info_s {
    uint64_t uncompressed_size;
    uint32_t chunk_count;
    size_t archive_size;
} xx_lzpis2_info;

/** Validate an LZPIS2 chunk chain and return its aggregate sizes. */
XXFC_API bool xx_lzpis2_parse_memory(const uint8_t *input, size_t input_size,
                                     xx_lzpis2_info *info);

/** Decode an LZPIS2 member into exactly output_size bytes. */
XXFC_API bool xx_lzpis2_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size, xx_lzpis2_info *info);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZPIS2_H */
