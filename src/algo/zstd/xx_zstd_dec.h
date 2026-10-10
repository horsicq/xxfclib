/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_ZSTD_DEC_H
#define XX_ZSTD_DEC_H

#include <stdbool.h>
#include <stddef.h>

bool xx_zstd_decode_frames(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written);

bool xx_zstd_decode_frames_bounded(const void *source, size_t source_size, void *destination, size_t destination_capacity, size_t *out_written);

/* Internal retry interface: distinguishes an output-capacity stop from other
 * failures. Later bytes can still be malformed. The caller may grow only on
 * this signal, up to an independently scanned bound. */
bool xx_zstd_decode_frames_bounded_retry(const void *source, size_t source_size, void *destination, size_t destination_capacity, size_t *out_written,
                                         bool *needs_more_output);

#endif /* XX_ZSTD_DEC_H */
