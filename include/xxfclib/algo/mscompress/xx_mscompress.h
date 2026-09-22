/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_mscompress.h @brief Microsoft SZDD/SZ LZSS decoder. */

#ifndef XXFCLIB_ALGO_MSCOMPRESS_H
#define XXFCLIB_ALGO_MSCOMPRESS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Decode one exact-size Microsoft LZSS stream.  The two documented variants
 * differ only in their encoded-match position bias: SZDD uses 16 and SZ uses
 * 18.  @p consumed receives the number of input bytes consumed through the
 * final token when non-NULL. */
XXFC_API bool xx_mscompress_lzss_decode(const uint8_t *input,
                                        size_t input_size,
                                        uint8_t *output,
                                        size_t output_size,
                                        unsigned position_bias,
                                        size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_MSCOMPRESS_H */
