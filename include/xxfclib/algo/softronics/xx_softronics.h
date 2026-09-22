/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_softronics.h @brief Softronics v2.00 LZW decoder. */

#ifndef XXFCLIB_ALGO_SOFTRONICS_H
#define XXFCLIB_ALGO_SOFTRONICS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decode the LSB-first, 9--12 bit GIF-style LZW stream used by Softronics
 * Compressed File v2.00 containers.  The stream must begin with CLEAR (256),
 * finish with END (257), and produce exactly @p output_size bytes.
 *
 * @param consumed_size receives the number of packed bytes consumed on
 * success; it may be NULL.
 */
XXFC_API bool xx_softronics_lzw_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_SOFTRONICS_H */
