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
#define XX_LZIP_V0_MIN_MEMBER_SIZE 28U

/** Return true when data begins with a syntactically valid Lzip header. */
XXFC_API bool xx_lzip_has_header(const uint8_t *data, size_t size);

/**
 * Decode a complete Lzip stream, including concatenated version 1 members
 * and single-member version 0 files with their shorter CRC/size trailer.
 *
 * The source extent must contain exactly the Lzip stream.  Each member's
 * LZMA end marker, decoded byte count, and CRC-32 trailer are verified.
 */
XXFC_API bool xx_lzip_decode_device(xx_io_device *source, int64_t source_offset, int64_t source_size, xx_io_device *destination, int64_t *output_size,
                                    size_t *member_count, xx_pd_struct *pd);

/** Decode the consecutive validated members at the start of an extent.
 * Ordinary trailing data is left outside consumed_size. A truncated or
 * corrupt next Lzip header/member remains an error. The strict API above
 * continues to require an exact stream extent.
 */
XXFC_API bool xx_lzip_decode_prefix_device(xx_io_device *source, int64_t source_offset, int64_t source_size, xx_io_device *destination,
                                           int64_t *output_size, size_t *member_count, int64_t *consumed_size, xx_pd_struct *pd);

/* Integrity findings of the lenient walk (bit set). */
#define XX_LZIP_ISSUE_DICTIONARY 0x01U    /* header dictionary code out of range */
#define XX_LZIP_ISSUE_CRC 0x02U           /* trailer CRC-32 differs from the data */
#define XX_LZIP_ISSUE_DATA_SIZE 0x04U     /* trailer data size differs */
#define XX_LZIP_ISSUE_MEMBER_SIZE 0x08U   /* trailer member size differs */
#define XX_LZIP_ISSUE_VERSION0_CHAIN 0x10U /* a version 0 member inside a chain */

/** As xx_lzip_decode_prefix_device, for damaged headers and trailers: an
 * out-of-range dictionary code is clamped, version 0 members may appear
 * anywhere in a chain, and each member's extent is where its LZMA end marker
 * really is. Every member must still decode cleanly to its end marker. Trailer
 * fields that disagree with the data do not fail the decode; they are
 * reported in *issues (XX_LZIP_ISSUE_*) for the caller to surface.
 */
XXFC_API bool xx_lzip_decode_prefix_device_lenient(xx_io_device *source, int64_t source_offset, int64_t source_size, xx_io_device *destination,
                                                   int64_t *output_size, size_t *member_count, int64_t *consumed_size, uint32_t *issues,
                                                   xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZIP_H */
