/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Decoder for the 13-byte LZMA-Alone transport used by .lzma and .tlz.
 */

#ifndef XXFCLIB_ALGO_LZMA_ALONE_H
#define XXFCLIB_ALGO_LZMA_ALONE_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_LZMA_ALONE_HEADER_SIZE 13U

/** Check whether a buffer begins with a syntactically valid LZMA-Alone header. */
XXFC_API bool xx_lzma_alone_has_header(const uint8_t *data, size_t size);

/**
 * Decode one complete LZMA-Alone stream.  The source extent must include its
 * 13-byte header and no unrelated trailing data.  An LZMA end marker is
 * required even when the header publishes a known expanded size.
 */
XXFC_API bool xx_lzma_alone_decode_device(xx_io_device *source,
                                          int64_t source_offset,
                                          int64_t source_size,
                                          xx_io_device *destination,
                                          int64_t *output_size,
                                          xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZMA_ALONE_H */
