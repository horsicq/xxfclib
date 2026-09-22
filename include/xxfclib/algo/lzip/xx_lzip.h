/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Lzip is a small transport around independently reset LZMA members.  This
 * interface decodes all consecutive members and verifies their trailers.
 */

#ifndef XXFCLIB_ALGO_LZIP_H
#define XXFCLIB_ALGO_LZIP_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_LZIP_HEADER_SIZE 6U
#define XX_LZIP_TRAILER_SIZE 20U
#define XX_LZIP_MIN_MEMBER_SIZE 36U

/** Return true when data begins with a syntactically valid Lzip header. */
XXFC_API bool xx_lzip_has_header(const uint8_t *data, size_t size);

/**
 * Decode a complete Lzip stream, including concatenated members.
 *
 * The source extent must contain exactly the Lzip stream.  Each member's
 * LZMA end marker, decoded byte count, and CRC-32 trailer are verified.
 */
XXFC_API bool xx_lzip_decode_device(xx_io_device *source,
                                    int64_t source_offset,
                                    int64_t source_size,
                                    xx_io_device *destination,
                                    int64_t *output_size,
                                    size_t *member_count,
                                    xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZIP_H */
