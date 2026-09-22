/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_RSVK_H
#define XXFCLIB_ALGO_RSVK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Member codec of the "RSVKDATA" / "DLIBDATA" container.
 *
 * A complete block-sorting compressor, structurally a cousin of the pre-bzip2
 * "bzip 0.21" pipeline but with its own tuning and no bzip container fields:
 *
 *     Witten/Neal/Cleary (CACM 1987) 16-bit adaptive arithmetic decoder
 *       -> Fenwick-style structured model over the MTF alphabet
 *       -> RUNA/RUNB bijective zero-run code
 *       -> inverse move-to-front
 *       -> inverse Burrows-Wheeler with an explicit sentinel row
 *
 * A member is a chain of blocks, each
 *
 *     +0x00 char[4] "DATA"
 *     +0x04 u32     CRC-32 (zlib polynomial) of the block's plaintext
 *     +0x08 u32     size of the arithmetic-coded payload that follows
 *     +0x0c u32     BWT primary index (the row the inverse walk starts on)
 *     +0x10 u32     row index of the BWT sentinel ("hole")
 *     +0x14 ...     payload
 *
 * A block's own plaintext length is not stored -- it falls out of the decode,
 * which ends on the 0x101 end-of-block symbol -- but the RSVK directory does
 * store each member's total uncompressed size, so no measuring entry point is
 * provided: @p output_size IS that size and a successful call produces exactly
 * that many bytes.  Every block's CRC-32 is verified.
 *
 * @param input       The member's complete chain of "DATA" blocks.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size The member's uncompressed size.
 * @param written     Receives the byte count produced (0 on failure).
 * @return true only on a complete decode of exactly @p output_size bytes.
 */
XXFC_API bool xx_rsvk_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);

#ifdef __cplusplus
}
#endif
#endif
