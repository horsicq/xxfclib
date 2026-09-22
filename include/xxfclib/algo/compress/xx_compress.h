/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_compress.h @brief Unix compress (.Z) LZW decoder. */

#ifndef XXFCLIB_ALGO_COMPRESS_H
#define XXFCLIB_ALGO_COMPRESS_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_COMPRESS_MAGIC0 UINT8_C(0x1f)
#define XX_COMPRESS_MAGIC1 UINT8_C(0x9d)

/** Return true when @p data begins with a structurally plausible .Z header. */
XXFC_API bool xx_compress_has_header(const uint8_t *data, size_t size);

/**
 * Decode one Unix compress transport range into @p destination.
 *
 * @p source_offset and @p source_size describe the complete stream, including
 * the three-byte 0x1f 0x9d flags header.  The decoder consumes precisely that
 * range, rejects malformed variable-width groups, and reports plaintext bytes
 * through @p output_size when it is non-NULL.
 */
XXFC_API bool xx_compress_decode_device(xx_io_device *source,
                                        int64_t source_offset,
                                        int64_t source_size,
                                        xx_io_device *destination,
                                        int64_t *output_size,
                                        xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_COMPRESS_H */
