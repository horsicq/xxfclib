/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ALDUS_LZW_H
#define XXFCLIB_ALGO_ALDUS_LZW_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decode one Gen-1 Aldus Setup block.  Codes are TIFF-style, MSB-first LZW
 * with Clear=256, EOD=257 and the early-change width rule. */
XXFC_API bool xx_aldus_lzw_decode_block(const uint8_t *input,
                                        size_t input_size,
                                        uint8_t *output,
                                        size_t output_size,
                                        size_t *written);

#ifdef __cplusplus
}
#endif
#endif
