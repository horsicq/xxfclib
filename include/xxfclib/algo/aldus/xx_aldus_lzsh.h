/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ALDUS_LZSH_H
#define XXFCLIB_ALGO_ALDUS_LZSH_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decode one Gen-3 Adobe LZSH block (stored or its LH5 static-Huffman form). */
XXFC_API bool xx_aldus_lzsh_decode_block(const uint8_t *input,
                                         size_t input_size,
                                         uint8_t *output,
                                         size_t output_size,
                                         size_t *written);

#ifdef __cplusplus
}
#endif
#endif
