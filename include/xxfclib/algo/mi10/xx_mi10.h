/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_MI10_H
#define XXFCLIB_ALGO_MI10_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode the backward byte-oriented LZ stream of the Amiga MI10 cruncher.
 *
 * MI10 is unrelated to the Microsoft compression formats with similarly short
 * names.  The stream is decoded from its END towards its start, and the
 * plaintext is likewise filled from its end towards its start:
 *
 *     input[0]            must be 0 (fixed marker)
 *     input[1]            the escape byte for this block
 *     input[2 .. n-1]     the token stream, read backwards from input[n-1]
 *
 * Tokens (each byte consumed by stepping the read cursor down):
 *
 *     b != ESC                    one literal byte b
 *     ESC, 0x00                   one literal ESC byte
 *     ESC, c (0 < c < 0x80)       match, distance = c, length = 3
 *     ESC, c (c >= 0x80), d       distance = ((c & 0x7f) << 4 | (d & 0x0f)) + 1
 *                                 d <  0x80: length = (d >> 4) + 4
 *                                 d >= 0x80, then e:
 *                                     length = ((d & 0x70) << 4 | e) + 12
 *
 * Deliberate asymmetry, kept from the reference: the SHORT form uses the
 * control byte as the distance verbatim (no +1), while the long form adds 1.
 * Matches copy FORWARD in the plaintext (towards higher addresses, i.e. the
 * already-produced tail), one byte at a time, so overlapping runs are expanded
 * by the same self-referential copy the original in-place unpacker performed.
 *
 * The archive header stores the plaintext length, so no measuring entry point
 * is provided: @p output_size IS that length and a successful call always
 * produces exactly that many bytes.  A complete decode additionally requires
 * the input cursor to land exactly on offset 2, i.e. the token stream must be
 * consumed to the byte, as the reference demands.
 *
 * @param input       Packed bytes, starting at the fixed 0 marker.
 * @param input_size  Length of @p input (at least 3).
 * @param output      Destination buffer.
 * @param output_size Plaintext length; exactly this many bytes are produced.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only on a complete decode of @p output_size bytes.
 */
XXFC_API bool xx_mi10_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_ALGO_MI10_H */
