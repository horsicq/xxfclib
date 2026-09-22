/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_wintersoft.h @brief Wintersoft "**++" AHUFF stream decoder. */

#ifndef XXFCLIB_ALGO_WINTERSOFT_H
#define XXFCLIB_ALGO_WINTERSOFT_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Wintersoft "**++" container carries two codecs, and the method tag in
 * its 8-byte header ("LZW " or "HUFF") is per FILE, not per member, so a call
 * site picks the codec once and uses it for every member.  The member's
 * plaintext length IS stored in the container, so neither codec needs a scan
 * entry point: output_size is the stored uncompressed size.
 *
 * ONLY THE AHUFF HALF LIVES HERE.  The container's "LZW " codec is Mark
 * Nelson's LZW15V, the same dialect as the headerless RAW_LZW15V streams, and
 * is not duplicated - see the note at the end of this comment.
 *
 * AHUFF - adaptive (FGK-style) Huffman over 0..255 plus 0x100 END and 0x101
 * ESCAPE, a verbatim port of Nelson's AHUFF.C as the archiver shipped it.
 * The tree starts holding only END and ESCAPE; a byte not yet in the tree
 * arrives as ESCAPE followed by EIGHT RAW BITS, after which the ESCAPE leaf is
 * split to make room for it.  Weights on the path to the root are bumped after
 * every symbol and nodes are swapped to keep the array sorted by descending
 * weight; when the ROOT weight reaches 0x8000 the whole tree is rebuilt with
 * every leaf weight halved as (w + 1) >> 1.
 *
 * Two details are deliberate and must not be "fixed":
 *  * A NODE KEEPS THE PARENT BELONGING TO ITS SLOT, not to its contents, so a
 *    swap does not move the parent link.  That is what makes the sibling
 *    property repair work in place.
 *  * The rebuild halves with (w + 1) >> 1, not w >> 1, so a weight of 1 stays
 *    1 rather than dropping to 0.
 *
 * UNVERIFIED IN THE REFERENCE TOO: every sample of the reference corpus is
 * "LZW ", so the reference AHUFF routine is itself only a careful
 * transcription and has never run against real data.  The rebuild path in
 * particular fires only after 0x8000 symbols.  Treat a first failure here as a
 * bug in this file, not in the sample.
 *
 * LZW15V: the container's other codec is the same algorithm as the standalone
 * LZW15V module (MSB-first, 9..15 bits, explicit 0x101 BUMP / 0x102 FLUSH,
 * first code 0x103, cap 0x8000, KwKwK).  It is intentionally NOT reimplemented
 * here.  A caller wiring the "LZW " tag to that module must confirm it carries
 * the two tolerances the container relies on: running out of input is an
 * END OF STREAM, not a failure (a member whose encoder omitted the explicit
 * 0x100 ends exactly that way), and the reference auto-bumps the width before
 * reading whenever its bump threshold has fallen below the next assignable
 * code.  Against a Nelson-compatible encoder the auto-bump is always exactly
 * one code short of firing, so it changes nothing for real members.
 */

/**
 * @brief Decode a Wintersoft AHUFF member into its stored plaintext size.
 *
 * @param input       Compressed bytes (the member's stored compressed size).
 * @param input_size  Length of @p input.
 * @param output      Receives exactly @p output_size plaintext bytes.
 * @param output_size The member's stored uncompressed size.
 * @param written     Receives the byte count produced.  Set on every path.
 * @return true only when exactly @p output_size bytes came out.
 */
XXFC_API bool xx_wintersoft_ahuff_decode_memory(const uint8_t *input,
                                                size_t input_size,
                                                uint8_t *output,
                                                size_t output_size,
                                                size_t *written);

/** Plain-signature alias of xx_wintersoft_ahuff_decode_memory(). */
XXFC_API bool xx_wintersoft_decode_memory(const uint8_t *input,
                                          size_t input_size, uint8_t *output,
                                          size_t output_size, size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_WINTERSOFT_H */
