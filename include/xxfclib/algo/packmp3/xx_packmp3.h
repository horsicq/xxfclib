/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PACKMP3_H
#define XX_PACKMP3_H
#include "xxfclib/xxfc_defs.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Reconstructs a ZIP method 94 packMP3 v1.0 stream as original MPEG-1 Layer III bytes.
 * Supports 32/44.1/48 kHz, mono/stereo, CBR/VBR, original tags, CRC and padding.
 * destination_size is the exact expected output size; out_written is zero on failure.
 * Input/output sizes cannot exceed INT32_MAX; decoder working heap has a 256 MiB
 * aggregate hard limit. The decoder implementation is LGPL-3.0-or-later. */
XXFC_API bool xx_packmp3_decompress_memory(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written);
#ifdef __cplusplus
}
#endif
#endif
