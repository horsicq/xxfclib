/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_LZX_H
#define XXFCLIB_ALGO_LZX_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

XXFC_API bool xx_lzx_cab_decode(const uint8_t *const *blocks,
                                const size_t *block_sizes,
                                const size_t *plain_sizes,
                                size_t block_count,
                                unsigned window_bits,
                                uint8_t *output,
                                size_t output_size,
                                size_t *written);

#ifdef __cplusplus
}
#endif
#endif
