/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_TERSE_H
#define XXFCLIB_ALGO_TERSE_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* IBM TERSE (TRSMAIN / AMATERSE) - the codec behind a "tersed" MVS data set.
 *
 * THIS IS NOT the "SPACK/PACK static Huffman" TERSE description that circulates
 * publicly.  TERSE nominally has two variants, PACK (a static Huffman coder)
 * and SPACK; the reference decoder this is ported from (XTERSEDecoder) handles
 * exactly one of them, the SPACK-style stream that every real TRSMAIN sample it
 * was built against uses, and so does this port.  There is no Huffman table
 * anywhere in the stream.  What the container holds is a 12-bit MSB-first code
 * stream driving a BINARY PAIR TREE with LRU node recycling:
 *
 *   * code 0 ends the stream, otherwise the symbol is code - 1;
 *   * symbol < 0x100 is a literal byte, symbol == 0x100 is a no-op, and
 *     symbol > 0x100 is an internal node expanded PRE-ORDER through LT then RT;
 *   * after every code the ring of nodes 0x101..0xFFE is advanced to the first
 *     zero-weight entry, that entry's old children have their weights
 *     DECREMENTED, LT is set to the previous code and RT to the current one,
 *     both of those have their weights INCREMENTED, and the node is moved to
 *     the MRU tail of the ring.
 *
 * Weights are 16-bit and WRAP: decrementing a zero weight yields 0xFFFF, which
 * is what keeps a freshly recycled node out of the free list until it is
 * released again.  That wrap is load bearing, not an overflow bug - do not
 * "fix" it with a saturating decrement.
 *
 * There is NO character-set translation.  Real samples are a mix of EBCDIC and
 * ASCII; translating either way would corrupt the output, so decoded bytes are
 * emitted exactly as produced.
 *
 * The container is a 12-byte header (only 4 bytes for the 0xA5698901 magic
 * variant) followed by the raw code stream.  There is no member name, no record
 * framing and NO STORED OUTPUT LENGTH - XTERSEArchive::parseContext() has to
 * call measure() to learn the size - which is why xx_terse_scan_memory() exists.
 *
 * All three entry points take the WHOLE FILE, header included: the header size
 * is a detection result, not a fixed skip.
 */

/* Largest stream the decoder will consider; mirrors the reference ceiling. */
#define XX_TERSE_MAX_UNCOMPRESSED_SIZE 0x20000000U

/**
 * @brief Recognise a TERSE container and report its header size.
 *
 * Reproduces the reference detector: the 0xA5698901 magic (4-byte header), or
 * one of three 12-byte header shapes followed by a sanity probe over the first
 * five 12-bit codes.
 *
 * @param input        The whole file.
 * @param input_size   Length of @p input.
 * @param header_size  Receives 4 or 12. May be NULL.
 * @return true when @p input looks like a TERSE container.
 */
XXFC_API bool xx_terse_detect(const uint8_t *input, size_t input_size,
                              size_t *header_size);

/**
 * @brief Decode a TERSE container into a buffer of known size.
 *
 * @param input        The whole file, header included.
 * @param input_size   Length of @p input.
 * @param output       Destination, never NULL.
 * @param output_size  Exact expected decoded size (from xx_terse_scan_memory).
 * @param written      Receives the decoded size; 0 on any failure.
 * @return true only when the stream reached its end code having produced
 *         exactly @p output_size bytes.
 */
XXFC_API bool xx_terse_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

/**
 * @brief Measure a TERSE container, which stores no decoded length.
 *
 * Runs the very same core routine with no output buffer.  Unlike a sliding
 * window codec this needs no window at all: a TERSE expansion reads only the
 * node tables, never previously emitted bytes, so discarding the output cannot
 * change the result.  The measure and the decode therefore cannot disagree.
 *
 * @param input       The whole file, header included.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a stream that would decode to more than this.
 * @param consumed    Receives the input bytes the stream occupies, header
 *                    included. May be NULL.
 * @param produced    Receives the decoded size. May be NULL.
 * @return true when the stream reached its end code within the limit.
 */
XXFC_API bool xx_terse_scan_memory(const uint8_t *input, size_t input_size,
                                   size_t max_output, size_t *consumed,
                                   size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
