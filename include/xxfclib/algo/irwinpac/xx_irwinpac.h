/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_IRWINPAC_H
#define XXFCLIB_ALGO_IRWINPAC_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file xx_irwinpac.h
 * @brief Irwin Magnetic Systems "IrwinPac" installation-file stream.
 *
 * The input starts at the first chunk header, i.e. right after the 20-byte
 * "IrwinPac" container header, and runs to the end of the file.  It is a chain
 * of independent blocks:
 *
 *   +0  u16  flag         0 = stored, 1 = compressed
 *   +2  u16  chunk size   INCLUDING this 10-byte header
 *   +4  u16  unpacked size of the block (16384 for every block but the last)
 *   +6  4    uninitialised encoder scratch - constant per file, never read
 *   +10      payload (chunk size - 10 bytes)
 *
 * A compressed payload is a bit stream consumed MSB-first inside each byte and
 * restarted byte-aligned at every block; matches never reach across a block
 * boundary, so the whole history a block needs is the block itself:
 *
 *   0 + <8 bits>                  literal byte
 *   1 + 1 + <7 bits>              match, distance 1..127
 *   1 + 0 + <11 bits>             match, distance 1..2047
 *   then the length, a nibble-escalating code:
 *       <2 bits> v, v < 3        -> 2 + v
 *       else <2 bits> v, v < 3   -> 5 + v
 *       else <4 bits> v, v < 15  -> 8 + v, and while v == 15 add 15 to the
 *                                   base and read the next nibble
 *
 * The encoder pads the tail of every block with about a dozen spare bytes, so
 * the block's unpacked size - not stream exhaustion - is the stop condition.
 * There is no terminator record: a clean end of input on a chunk boundary is
 * how the member finishes.
 */

/**
 * @brief Decode a complete IrwinPac chunk chain.
 * @param input       Stream starting at the first chunk header.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity of @p output; also the point at which the decode
 *                    stops early, matching the reference's nProcessedLimit.
 * @param written     Receives the produced size; set on every path.
 * @return true only on a complete decode.
 */
XXFC_API bool xx_irwinpac_decode_memory(const uint8_t *input, size_t input_size,
                                        uint8_t *output, size_t output_size,
                                        size_t *written);

/**
 * @brief Measure an IrwinPac stream without a destination buffer.
 *
 * The container's uncompressed size is not a stored field: it is the sum of
 * the per-chunk unpacked sizes, so a reader that wants one has to walk the
 * chain.  This runs the identical decoder and discards each block, so the
 * measurement and the decode can never disagree.
 *
 * @param max_output  Refuse a stream decoding to more than this.
 * @param consumed    Receives the input bytes the chain occupies. May be NULL.
 * @param produced    Receives the decoded size. May be NULL.
 */
XXFC_API bool xx_irwinpac_scan_memory(const uint8_t *input, size_t input_size,
                                      size_t max_output, size_t *consumed,
                                      size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
