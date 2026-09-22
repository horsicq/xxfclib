/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_ALGO_LZOP_H
#define XXFCLIB_ALGO_LZOP_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_LZOP_MAGIC_SIZE 9U

/** Return true for the fixed LZOP magic prefix. */
XXFC_API bool xx_lzop_has_header(const uint8_t *data, size_t size);

/** Decode one or more concatenated LZOP streams into destination. */
XXFC_API bool xx_lzop_decode_device(xx_io_device *source,
                                    int64_t source_offset,
                                    int64_t source_size,
                                    xx_io_device *destination,
                                    int64_t *output_size,
                                    size_t *stream_count,
                                    xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZOP_H */
