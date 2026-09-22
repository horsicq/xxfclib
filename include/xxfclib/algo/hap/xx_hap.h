/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_HAP_H
#define XXFCLIB_ALGO_HAP_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Harri Hirvola's HAP archiver, method 0x16: a five-order PPM model driven by
 * a 16-bit Witten-Neal-Cleary arithmetic decoder.  The container stores the
 * member's plaintext size, so no measuring entry point is needed; output_size
 * is the declared size and is decoded exactly. */
XXFC_API bool xx_hap_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif

#endif
