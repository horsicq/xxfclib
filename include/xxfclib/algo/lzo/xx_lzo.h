/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_ALGO_LZO_H
#define XXFCLIB_ALGO_LZO_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Decode one bounded LZO1X block into an exactly sized caller buffer. */
XXFC_API bool xx_lzo1x_decompress(const uint8_t *input, size_t input_size,
                                  uint8_t *output, size_t output_size,
                                  size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZO_H */
