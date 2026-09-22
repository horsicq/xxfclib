/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_ALGO_TARX2_H
#define XXFCLIB_ALGO_TARX2_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_TARX2_HEADER_SIZE 16U
#define XX_TARX2_BLOCK_SIZE 8U

/** Check TARX version 2's fixed 16-byte transport header. */
XXFC_API bool xx_tarx2_has_header(const uint8_t *data, size_t size);

/**
 * Apply TARX2's fixed-key ECB transform to complete blocks.  Input plaintext
 * uses big-endian halves; ciphertext uses the format's little-endian halves.
 * This low-level helper does not add the header, Gzip wrapping, or padding.
 */
XXFC_API bool xx_tarx2_encrypt_blocks(const uint8_t *source,
                                      uint8_t *destination, size_t size);

/**
 * Decrypt the fixed-key Blowfish transport, unwrap its Gzip member, and emit
 * the ordinary TAR payload.  The source range includes the TARX2 header.
 */
XXFC_API bool xx_tarx2_decode_device(xx_io_device *source,
                                     int64_t source_offset,
                                     int64_t source_size,
                                     xx_io_device *destination,
                                     int64_t *output_size,
                                     xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_TARX2_H */
