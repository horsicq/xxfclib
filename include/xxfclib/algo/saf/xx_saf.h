/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_SAF_H
#define XXFCLIB_ALGO_SAF_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stac Electronics SAF archive member codec.
 *
 * LZ77 over a 2 KiB ring buffer with a fixed, table-less token encoding, read
 * MSB first:
 *
 *     9 bit token t
 *       t <  0x100        literal byte t
 *       t >= 0x100, c = t & 0xff
 *         c == 0x81       run of the previous byte (distance 1)
 *         c >  0x80       match at distance (c & 0x7f)
 *         c == 0x80       end of stream
 *         c <  0x80       match at distance c * 16 + (next 4 bits)
 *
 *     match length: 2 bits L; if L == 3 add 2 more bits e, and if e == 3 keep
 *     adding 4 bit nibbles while each is 15.  The emitted length is L + 2.
 *
 * The window starts zeroed and is used as a true ring, so a match may legally
 * reach back into the not-yet-written part of it.  That is not a malformed
 * stream here: the reference decoder produces the ring's stale (initially
 * zero) content and real members rely on it.  See the comment in the .c file.
 *
 * The SAF container stores the member's plaintext size, so no measuring entry
 * point is provided: @p output_size IS that size and a successful call always
 * produces exactly that many bytes.
 *
 * @param input       Compressed bytes (the member's packed extent).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size The member's plaintext size.
 * @param written     Receives the byte count produced (0 on failure).
 * @return true only on a complete decode of exactly @p output_size bytes.
 */
XXFC_API bool xx_saf_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

/**
 * @brief As xx_saf_decode_memory(), for a member whose method byte is known.
 *
 * @p method is the SAF member header's method byte: 3 means the packed extent
 * is a single stream (what xx_saf_decode_memory() assumes), anything else
 * means it is a chain of [uint32 little-endian length][stream] chunks, each of
 * which restarts the bit reader and the 2 KiB window.
 */
XXFC_API bool xx_saf_decode_memory_method(const uint8_t *input,
                                          size_t input_size, uint32_t method,
                                          uint8_t *output, size_t output_size,
                                          size_t *written);

#ifdef __cplusplus
}
#endif
#endif
