/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_wpk.h @brief WPK (Watcom installer "pack") LZSS decoders. */

#ifndef XXFCLIB_ALGO_WPK_H
#define XXFCLIB_ALGO_WPK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WPK - the Watcom installer "pack" archive (magic 03 24 01 01 / 03 24 33 01).
 * The directory record flag byte at +0x10 carries the member's method in bit 7:
 * clear -> method A, set -> method B.  Both methods are LZSS over a 4096-byte
 * ring buffer preset to 0x20 (' '), read MSB first, and both share LHA's
 * "-lh1-" position tables for the distance:
 *
 *     i    = next 8 bits
 *     n    = d_len[i >> 4] - 2 further bits, shifted into i (byte arithmetic)
 *     dist = d_code[i] * 64 + (i & 0x3F)
 *     src  = (pos - dist - 1) & 0xFFF
 *
 * Method A adds a Huffman coded literal/length alphabet of at most 0x13B
 * symbols whose code lengths are transmitted first, as one count byte followed
 * by count + 1 control bytes:
 *
 *     b & 0x80 == 0 : the next (b >> 4) + 1 symbols all get length (b & 0xF) + 1
 *     b & 0x80 != 0 : skip (b & 0x7F) + 1 symbols, which get no table entry
 *
 * Method B has no Huffman stage at all: one flag bit, then either eight literal
 * bits or a six-bit raw match length followed by the same distance encoding.
 *
 * Load bearing, and none of it guessable:
 *
 *  * THE CODE-LENGTH SORT IS UNSTABLE AND ITS TIE-BREAK IS PART OF THE FORMAT.
 *    The order the sort happens to leave symbols in WITHIN a run of equal
 *    lengths decides which symbol gets which canonical code, so the two
 *    unstable sorts are transliterated instruction for instruction.  Any
 *    "clean" or stable sort is wrong for every real member, and wrong quietly.
 *  * WHICH SORTER AN ARCHIVE WANTS IS NOT IN ITS HEADER; the caller probes it
 *    once per archive (see xx_wpk_decode_method_a_memory's `consumed`).
 *  * CANONICAL CODES ARE HANDED OUT BACKWARDS, from the LAST table entry to the
 *    first, in a 16-bit left-justified space.
 *  * A MATCH LENGTH IS symbol - 0xFD, so the first match symbol means three
 *    bytes.
 *
 * The plaintext length is stored per member in the WPK directory, so there is
 * no scan entry point here: `output_size` is the member's stored uncompressed
 * size and a decode that does not land on it exactly is a failure.
 */

/** Tie-break order of the code-length sort.  Not derivable from the header. */
typedef enum xx_wpk_sorter_e {
    XX_WPK_SORTER_A = 0,     /**< simple quicksort with an explicit stack */
    XX_WPK_SORTER_B = 1,     /**< BSD-style qsort: shell sort < 16, median of 3/9 */
    XX_WPK_SORTER_STABLE = 2 /**< stable by length; wrong for every archive seen */
} xx_wpk_sorter;

/**
 * @brief Decode a WPK method A (Huffman + LZSS) member.
 *
 * @param input       Compressed bytes (the member's stored compressed size).
 * @param input_size  Length of @p input.
 * @param sorter      One of xx_wpk_sorter; the archive-wide probe result.
 * @param output      Receives exactly @p output_size plaintext bytes.
 * @param output_size The member's stored uncompressed size.
 * @param written     Receives the byte count produced.  Set on every path.
 * @param consumed    Receives the input bytes the stream occupies, which is
 *                    what the directory's CRC-32 covers - the caller CRCs that
 *                    prefix to confirm the sorter choice.  May be NULL.
 * @return true only when exactly @p output_size bytes came out.
 */
XXFC_API bool xx_wpk_decode_method_a_memory(const uint8_t *input,
                                            size_t input_size, int sorter,
                                            uint8_t *output, size_t output_size,
                                            size_t *written, size_t *consumed);

/**
 * @brief Decode a WPK method B (plain LZSS) member.  See method A for the
 *        meaning of @p consumed.
 */
XXFC_API bool xx_wpk_decode_method_b_memory(const uint8_t *input,
                                            size_t input_size, uint8_t *output,
                                            size_t output_size,
                                            size_t *written, size_t *consumed);

/**
 * @brief Plain-signature entry point: method A with XX_WPK_SORTER_A.
 *
 * Convenience only.  A reader that has not run the archive-wide sorter probe
 * must not rely on it: 5 of the 127 reference archives need XX_WPK_SORTER_B,
 * and they decode to plausible garbage under XX_WPK_SORTER_A.
 */
XXFC_API bool xx_wpk_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_WPK_H */
