/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lzdiet.h @brief lZdIeT chunked LZW decoder. */

#ifndef XXFCLIB_ALGO_LZDIET_H
#define XXFCLIB_ALGO_LZDIET_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lzdiet_info_s {
    uint32_t uncompressed_size;
    uint16_t chunk_count;
    size_t archive_size;
} xx_lzdiet_info;

/** Parse and bounds-check an lZdIeT header and its complete chunk table. */
XXFC_API bool xx_lzdiet_parse_memory(const uint8_t *input, size_t input_size,
                                     xx_lzdiet_info *info);

/** Decode lZdIeT chunks into exactly output_size bytes. */
XXFC_API bool xx_lzdiet_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size, xx_lzdiet_info *info);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZDIET_H */
