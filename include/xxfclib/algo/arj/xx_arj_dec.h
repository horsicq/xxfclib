/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ARJ_DEC_H
#define XXFCLIB_ALGO_ARJ_DEC_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decode one bounded ARJ member.
 *
 * Supported methods are stored (0), compressed (1-3), and fastest (4).
 * The caller supplies the exact packed and unpacked sizes from the ARJ header.
 */
XXFC_API bool xx_arj_decode_memory(uint8_t method,
                                   const uint8_t *input,
                                   size_t input_size,
                                   uint8_t *output,
                                   size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_ARJ_DEC_H */
