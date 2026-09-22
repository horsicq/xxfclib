/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ARCV4_H
#define XXFCLIB_ALGO_ARCV4_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Eschalon Setup ARCV v4 method 2: LSB-first adaptive Huffman plus LZ77. */
XXFC_API bool xx_arcv4_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

#ifdef __cplusplus
}
#endif

#endif
