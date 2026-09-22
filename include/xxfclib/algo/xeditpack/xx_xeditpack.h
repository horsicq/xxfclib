/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_XEDITPACK_H
#define XXFCLIB_ALGO_XEDITPACK_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The CMS COPYFILE PACK / XEDIT PACK byte codec: the payload of an XEDIT PACK
 * container, i.e. everything after its 8-byte header.  A plain run/literal
 * coder, no dictionary, no bit packing:
 *
 *   0x00..0x77          write (c + 1) EBCDIC blanks (0x40)
 *   0x78  u8  n         write (n + 1) blanks
 *   0x79  u16 n         write (n + 1) blanks
 *   0x7A  u8  n, u8 b   write b, (n + 1) times
 *   0x7B  u16 n, u8 b   write b, (n + 1) times
 *   0x7C..0x7F          exact aliases of 0x78..0x7B
 *   0x80..0xF7          copy (c - 0x80 + 1) literal bytes
 *   0xF8  u8  n         copy (n + 1) literal bytes
 *   0xF9  u16 n         copy (n + 1) literal bytes
 *   0xFC, 0xFD          exact aliases of 0xF8, 0xF9
 *   0xFF                end of stream
 *   0xFA, 0xFB, 0xFE    malformed; the walk stops, keeping what it produced
 *
 * Load bearing, from the reference (XArchive/Algos/xxeditpackdecoder.cpp):
 *  - every count is biased by one, including the two count-in-the-opcode
 *    ranges;
 *  - the 16-bit counts are BIG endian (mainframe format);
 *  - a truncated stream is NOT an error.  A literal copy that runs past the
 *    end still appends the bytes that ARE present and only then ends the walk,
 *    while a run opcode whose operands are incomplete appends nothing at all.
 *
 * The output stays in EBCDIC; the reference applies no code-page translation.
 */

/* The reference's XXEditPackDecoder::MAX_UNCOMPRESSED_SIZE: a crafted stream
 * of three-byte run opcodes expands by up to 0x10000:3, so the walk needs a
 * ceiling of its own, independent of the input size.  Pass this as
 * @p max_output to reproduce the reference exactly. */
#define XX_XEDITPACK_MAX_OUTPUT ((size_t)0x10000000U)

/**
 * @brief Decode an XEDIT PACK payload into a caller buffer.
 *
 * The walk itself runs with the reference's ceiling (raised to @p output_size
 * when the caller offers more), while @p output_size bounds what is written:
 * a stream encoding more bytes than the buffer holds fails rather than
 * truncating, and @p written is 0 on every failure.  The two limits are kept
 * apart on purpose -- a truncated literal copy reserves its full nominal count
 * against the walk's ceiling but emits only the bytes present, so folding them
 * into one would make a damaged member fail to decode at the very length
 * xx_xeditpack_scan_memory() reported for it.
 *
 * When the size came from the scan, @p written equals @p output_size exactly.
 */
XXFC_API bool xx_xeditpack_decode_memory(const uint8_t *input,
                                         size_t input_size, uint8_t *output,
                                         size_t output_size, size_t *written);

/**
 * @brief Measure an XEDIT PACK payload without storing it.
 *
 * The container stores no plaintext length, so this is how a reader learns the
 * member size.  It runs the very same walk as the decoder with the output
 * discarded, so the two can never disagree about where a damaged stream ends.
 *
 * @param input       Packed bytes.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a stream that would decode to more than this.
 * @param consumed    Receives the input bytes the stream occupies (clamped to
 *                    @p input_size). May be NULL.
 * @param produced    Receives the decoded size. May be NULL.
 * @return true when the walk finished within the limit and produced at least
 *         one byte; this mirrors XXEditPackDecoder::measure().
 */
XXFC_API bool xx_xeditpack_scan_memory(const uint8_t *input, size_t input_size,
                                       size_t max_output, size_t *consumed,
                                       size_t *produced);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_ALGO_XEDITPACK_H */
