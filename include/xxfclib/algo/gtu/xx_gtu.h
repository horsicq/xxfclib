/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_GTU_H
#define XXFCLIB_ALGO_GTU_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* IBM GTU (OS/2 corrective-service kit) member body: a chain of frames, each
 * [i32 rawSize][i32 packedSize] followed by one complete Okumura LZARI stream.
 * The container stores the member's plaintext size, so no measuring entry
 * point is needed; output_size is the declared size and is decoded exactly. */
XXFC_API bool xx_gtu_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif

#endif
