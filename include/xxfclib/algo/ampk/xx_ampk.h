/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_AMPK_H
#define XXFCLIB_ALGO_AMPK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decoders used by methods 1 and 2 of the Amiga AMPK archive format. */
XXFC_API bool xx_ampk_lzari_decode_memory(const uint8_t *input,
                                          size_t input_size,
                                          uint8_t *output,
                                          size_t output_size,
                                          size_t *written);
XXFC_API bool xx_ampk_lzss_decode_memory(const uint8_t *input,
                                         size_t input_size,
                                         uint8_t *output,
                                         size_t output_size,
                                         size_t *written);

#ifdef __cplusplus
}
#endif

#endif
