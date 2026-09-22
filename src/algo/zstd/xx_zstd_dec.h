/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_ZSTD_DEC_H
#define XX_ZSTD_DEC_H

#include <stdbool.h>
#include <stddef.h>

bool xx_zstd_decode_frames(const void *source, size_t source_size,
                           void *destination, size_t destination_size,
                           size_t *out_written);

#endif /* XX_ZSTD_DEC_H */
