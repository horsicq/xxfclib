/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_WINZIPJPEG_H
#define XX_WINZIPJPEG_H
#include "xxfclib/xxfc_defs.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Reconstructs a ZIP method 96 sequential JPEG file without loss of the original bytes.
 * destination_size is the exact expected output size; out_written is zero on failure.
 * Decoder working allocations have an aggregate 64 MiB hard limit; frames requiring
 * larger coefficient slices are rejected. Caller-owned input/output are separate. */
XXFC_API bool xx_winzipjpeg_decompress_memory(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written);
#ifdef __cplusplus
}
#endif
#endif
