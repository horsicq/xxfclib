/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_corelltec.h
 * @brief Codec of the Corel/LEAD Technologies "LTEC" installer archive
 *        (SETUP.LTA).  Ported from XArchive's XCorelLtecDecoder.
 *
 * The payload is an LHA-family static-Huffman LZ77 stream: NC=510/CBIT=9
 * literal+length table, NT=19/TBIT=5 pre-table with the i_special=3 two-bit
 * zero run, a position table read with PBIT=5, THRESHOLD=3 and MAXMATCH=256.
 * Two things stop it from being ordinary -lh5-:
 *
 *  1. Each block opens with SIXTEEN BITS that carry no information.  The LHA
 *     block-symbol count follows them.
 *  2. The position alphabet is not fixed at 14/16/17.  Its declared symbol
 *     count varies from block to block and runs past 17, so a distance can
 *     need more than sixteen extra bits; LTEC_NP is the alphabet ceiling and
 *     the bit reader is 32-bit-wide accordingly.
 *
 * The container is SOLID: one block holds several consecutive members and a
 * member starts at an arbitrary byte offset inside the block's plaintext.
 * The window is the block plaintext produced so far - a flat buffer, never a
 * ring pre-filled with spaces - so decoding a member that starts at offset N
 * requires producing the N bytes ahead of it.  That is what
 * xx_corelltec_decode_member() does.
 *
 * Both container fields a caller needs (the block's plaintext size and the
 * member's offset inside it) come from the LTEC directory, so the plaintext
 * length is always known up front and no measuring entry point is required.
 *
 * The packed input MUST run a few bytes past the block's directory extent:
 * consecutive blocks overlap and the last symbols of a block live in the
 * first bytes of the next one (measured worst case +16 bits).  Inside that
 * 16-bit margin the reader serves zero bits, exactly as the reference does.
 */

#ifndef XXFCLIB_ALGO_CORELLTEC_H
#define XXFCLIB_ALGO_CORELLTEC_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode the first @p output_size bytes of an LTEC block's plaintext.
 *
 * This is the whole-block entry point and the one a non-solid caller wants:
 * pass the block's plaintext size as @p output_size.  It is equivalent to the
 * reference XCorelLtecDecoder::decode() called with an empty property blob.
 *
 * @param input       The block, starting at its first byte (the two
 *                    uninformative ones included), running a few bytes past
 *                    the block's own extent where the container allows.
 * @param input_size  Length of @p input.
 * @param output      Destination, at least @p output_size bytes.
 * @param output_size How much of the block's plaintext to produce.
 * @param written     Receives the produced byte count.  Set on every path.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_corelltec_decode_memory(const uint8_t *input,
                                         size_t input_size, uint8_t *output,
                                         size_t output_size, size_t *written);

/**
 * @brief Decode one member of a solid LTEC block.
 *
 * Produces @p offset_in_block + @p output_size bytes of block plaintext into
 * scratch memory and copies out the member's slice.  The prefix cannot be
 * skipped: a match may reach back to the block's very first byte.
 *
 * @param input           The block (see xx_corelltec_decode_memory).
 * @param input_size      Length of @p input.
 * @param offset_in_block The member's offset in the block's plaintext.
 * @param output          Destination, at least @p output_size bytes.
 * @param output_size     The member's uncompressed size.
 * @param written         Receives the produced byte count.  Set on every path.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_corelltec_decode_member(const uint8_t *input,
                                         size_t input_size,
                                         size_t offset_in_block,
                                         uint8_t *output, size_t output_size,
                                         size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_CORELLTEC_H */
