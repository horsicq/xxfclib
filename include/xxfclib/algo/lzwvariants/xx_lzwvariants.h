/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lzwvariants.h
 *  @brief Two LZW dialects that no other xxfclib entry point can express.
 *
 *  Both containers that use these codecs store the plaintext length in their
 *  own header, so neither needs a measuring (`scan`) entry point:
 *    - K-BOOM: uint32 at header offset 4 (archives/xkboomarchive.cpp).
 *    - LZWD / NewWave: uint32 at header offset 0 (archives/xlzwdarchive.cpp).
 *
 *  A third dialect was surveyed for this module and deliberately left out:
 *  the ZPAK member codec is the plain GIF dialect (LSB-first, 9..12 bits,
 *  CLEAR=256, END=257) and is already served by
 *  xx_softronics_lzw_decompress_memory().  ZPAK's own chunk framing is
 *  container work for the reader, not codec work.
 */

#ifndef XXFCLIB_ALGO_LZWVARIANTS_H
#define XXFCLIB_ALGO_LZWVARIANTS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief K-BOOM LZW.
 *
 * Not a code-table LZW at all: the dictionary is an explicit 0x2000-entry
 * TRIE with sibling lists, every code is a fixed 13 bits, and there is no
 * clear code.  Two properties make it unlike every other LZW here:
 *
 *  - A token extends the PREVIOUS phrase by up to four bytes of the current
 *    one (not the usual single byte), walking existing trie children first
 *    and only allocating once the walk fails.
 *  - When the trie fills, slots are RECYCLED by a rotating cursor that looks
 *    for a childless node and unlinks it from its parent's sibling list.
 *    The table never resets.
 *
 * Bit order is its own thing too: 13-bit codes are taken from the top of a
 * 32-bit accumulator that is refilled one little-endian 16-bit word at a
 * time, the word being *added* into the accumulator rather than OR-ed.
 *
 * Faithfulness note: the reference (Algos/xkboomdecoder.cpp) lets the last
 * phrase run past the declared plaintext size and then truncates.  This does
 * the same by dropping the overrun bytes of that final phrase; it is not a
 * capacity failure, and a later reader must not "fix" it into one.
 *
 * @param input       Packed member payload (the code stream starts at [0]).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Declared plaintext size; both the cap and the
 *                    requirement for success.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_lzwvariants_kboom_decode_memory(const uint8_t *input,
                                                 size_t input_size,
                                                 uint8_t *output,
                                                 size_t output_size,
                                                 size_t *written);

/**
 * @brief NewWave "LZWD" LZW (HANDLE_METHOD_LZWD_LZW).
 *
 * MSB-first, 9..12 bits, CLEAR=0x100, END=0x101, first assignable slot
 * 0x102, early width change.  That much is TIFF/PDF-shaped - but the one
 * rule that makes it a separate codec is what happens at the top width:
 *
 *   instead of stalling at a full 4096-entry table and waiting for an
 *   explicit CLEAR (what xx_aldus_lzw_decode_block does, and what PDF
 *   LZWDecode does), this dialect SELF-RESTARTS: the table drops back to
 *   0x102, the width back to 9, and the previous-code state is dropped.
 *   The explicit CLEAR the encoder then writes is read at nine bits and is
 *   a no-op.  A decoder that stalls instead desynchronises immediately.
 *
 * Deliberate, do not "fix": the width test is `(free + 1) >= (1 << width)`
 * and it runs after a literal token as well as after a phrase token, but is
 * skipped by an explicit CLEAR.
 *
 * Unlike the K-BOOM entry point above, an overrunning final phrase is a
 * failure here, matching the reference, which keeps the overrun and then
 * fails its exact-size test.
 *
 * @param input       Packed stream (starts immediately after the 11-byte
 *                    LZWD header; the first code is a 9-bit CLEAR).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Declared plaintext size; both the cap and the
 *                    requirement for success.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_lzwvariants_newwave_decode_memory(const uint8_t *input,
                                                   size_t input_size,
                                                   uint8_t *output,
                                                   size_t output_size,
                                                   size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZWVARIANTS_H */
