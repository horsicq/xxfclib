/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_NPACK_H
#define XXFCLIB_ALGO_NPACK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NPack ("MSTSM") payload codec -- the Stac LZS block that follows the
 * container's 5-byte ASCII magic.  The input handed to these entry points
 * starts AFTER that magic.
 *
 * Grammar (MSB-first bit stream, 2048-byte zero-filled history window):
 *
 *   0 <8 bits>            literal byte
 *   1 0 <11 bits>         match, distance = code; code 0 is invalid
 *   1 1 <7 bits>          match, distance = code; code 0 is the STOP code
 *   length prefix: 00 -> 2  01 -> 3  10 -> 4  1100 -> 5  1101 -> 6
 *                  1110 -> 7  1111 -> 8 + sum of 4-bit groups, the last group
 *                             being the first one below 15
 *
 * The container stores no plaintext length, so a reader must measure the
 * stream with xx_npack_scan_memory() before it can allocate.
 */

/**
 * @brief Decode one NPack LZS block.
 *
 * @param input       Payload bytes (after the "MSTSM" magic).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity, and the exact length the block must decode to.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only when the block reached its stop code and produced exactly
 *         @p output_size bytes.
 */
XXFC_API bool xx_npack_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

/**
 * @brief Measure an NPack block without knowing its decoded size.
 *
 * Runs the identical core routine but keeps only the 2048-byte sliding
 * window, so the measure and the decode can never disagree.
 *
 * @param input       Payload bytes (after the "MSTSM" magic).
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a block that would decode to more than this.
 * @param consumed    Receives the payload bytes the block occupies, rounded up
 *                    to a byte boundary. May be NULL.
 * @param produced    Receives the decoded size. May be NULL.
 * @return true when the block reached its stop code within the limit.
 */
XXFC_API bool xx_npack_scan_memory(const uint8_t *input, size_t input_size,
                                   size_t max_output, size_t *consumed,
                                   size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
