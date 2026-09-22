/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ARCFS_LZW_H
#define XXFCLIB_ALGO_ARCFS_LZW_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ARC's 0x90 run filter, used by ArcFS "packed" members. */
XXFC_API bool xx_arcfs_rle90_decode_memory(const uint8_t *input,
                                           size_t input_size,
                                           uint8_t *output,
                                           size_t output_size,
                                           size_t *written);

/* ArcFS LSB-first LZW.  A clear code is present and code-width changes are
 * padded to groups of eight codes, as required by the original ARC codec. */
XXFC_API bool xx_arcfs_lzw_decode_memory(const uint8_t *input,
                                         size_t input_size,
                                         uint8_t *output,
                                         size_t output_size,
                                         uint8_t max_bits,
                                         bool apply_rle90,
                                         size_t *written);

#ifdef __cplusplus
}
#endif

#endif
