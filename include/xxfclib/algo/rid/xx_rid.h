/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_RID_H
#define XXFCLIB_ALGO_RID_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Payload codec of the "RID" OS/2 installer archive (OS2YOU / LANTERM /
 * TERM2 / DRIVERS packages, 1998-99).
 *
 * A member's payload is not one compressed stream but a chain of framed
 * blocks, each frame being
 *
 *     u16 block_size (little endian)   u8 block_type
 *
 * followed by block_size payload bytes.  Three block types exist:
 *
 *     0x00  stored - the block_size bytes are plaintext, copied verbatim
 *     0x01  packed - the block_size bytes are ONE COMPLETE PKWARE Data
 *                    Compression Library stream, prelude and end-of-stream
 *                    code included.  The decoder restarts per block, so
 *                    blocks share no window and no bit buffer; the plaintext
 *                    is simply concatenated.
 *     0xff  end    - always with block_size == 0, terminates the member.
 *
 * The container stores the member's uncompressed size, so the decode entry
 * point is the one a reader needs; the scan is for the parse-time walk that
 * has to learn a member's extent before it has anywhere to put the plaintext.
 */

/**
 * @brief Decode one member's whole block chain.
 *
 * @p input must start at the first frame and cover the chain exactly, up to
 * and including the terminator; @p output_size is the member header's
 * uncompressed size.  Succeeds only when the chain terminates exactly at the
 * end of @p input and produces exactly @p output_size bytes.
 */
XXFC_API bool xx_rid_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

/**
 * @brief Walk a chain without keeping the plaintext.
 *
 * Follows the frames from @p input, decoding packed blocks through a sliding
 * window only, and reports the byte extent of the chain in @p consumed and
 * the plaintext length in @p produced.  A chain that would produce more than
 * @p max_output bytes is a failure, not a truncation.
 */
XXFC_API bool xx_rid_scan_memory(const uint8_t *input, size_t input_size,
                                 size_t max_output, size_t *consumed,
                                 size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
