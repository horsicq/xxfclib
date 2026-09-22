/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_CLAYLZ_H
#define XXFCLIB_ALGO_CLAYLZ_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a Clay ("Clay" / "ClayE") member stream of known plaintext
 *        length.
 *
 * LZ77 over a small ring with four FIXED, LSB-first prefix codes that the
 * format never transmits; they are reproduced here as tables.
 *
 * Stream layout
 *   byte 0  literal mode.  0 = literals are plain 8-bit, 1 = literals come
 *           through the 256-entry literal code.  No other value is legal.
 *   byte 1  window exponent, only 4, 5 or 6.  The ring is 0x40 << e bytes,
 *           so 1 KiB, 2 KiB or 4 KiB, and the same e is the width of the low
 *           half of a distance.
 *   then    a bit stream, LSB-first.
 *
 * Token
 *   bit 0 -> literal (see byte 0).
 *   bit 1 -> match.  A 16-entry length class gives base + extra bits; the
 *           value 0x207 (the largest the table can express) is the end of
 *           stream marker, not a length.  A 64-entry distance class gives the
 *           high half; the low half is `e` bits, except for a length of
 *           exactly 2, where it is 2 bits and the class is scaled by 4.  The
 *           match source is (writePos - distance - 1) masked to the ring, and
 *           it is copied byte by byte, so a match may overlap itself.
 *
 * The container stores the member's decoded size, so @p output_size IS that
 * length and a successful call always produces exactly that many bytes; no
 * measuring entry point is provided.
 *
 * @param input       Compressed bytes (the member stream, header included).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Plaintext length; exactly this many bytes are produced.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only on a complete decode of @p output_size bytes.
 */
XXFC_API bool xx_claylz_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

#ifdef __cplusplus
}
#endif
#endif
