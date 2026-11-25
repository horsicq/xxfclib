/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_WAVPACK_H
#define XX_WAVPACK_H
#include "xxfclib/xxfc_defs.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Reconstructs a ZIP method 97 lossless RIFF/WAVE file, including stored header and trailer.
 * destination_size is the exact expected output size; out_written is zero on failure. */
XXFC_API bool xx_wavpack_decompress_memory(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written);
#ifdef __cplusplus
}
#endif
#endif
