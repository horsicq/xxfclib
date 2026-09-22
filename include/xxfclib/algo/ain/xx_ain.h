/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_AIN_H
#define XXFCLIB_ALGO_AIN_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decode an AIN solid-stream range, discarding skip_size bytes first. */
XXFC_API bool xx_ain_decode_memory(const uint8_t *input, size_t input_size,
                                   size_t skip_size, uint8_t *output,
                                   size_t output_size, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
