/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_ALGO_TARX_H
#define XXFCLIB_ALGO_TARX_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_TARX1_MAGIC_SIZE 4U

/** Check for the fixed TARX v1 signature. */
XXFC_API bool xx_tarx1_has_header(const uint8_t *data, size_t size);

/** Recover TARX v1's embedded stream key and decode its TAR payload. */
XXFC_API bool xx_tarx1_decode_device(xx_io_device *source,
                                     int64_t source_offset,
                                     int64_t source_size,
                                     xx_io_device *destination,
                                     int64_t *output_size,
                                     xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_TARX_H */
