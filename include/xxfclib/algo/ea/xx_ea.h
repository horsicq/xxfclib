/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_EA_H
#define XXFCLIB_ALGO_EA_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Three unrelated Electronic Arts codecs.  They share a vendor and nothing
 * else -- an LZW dialect, an Okumura LZSS dialect and the RefPack/QFS LZ77
 * grammar -- so they share no code here either, only this header and this
 * translation unit.
 */

/**
 * @brief Electronic Arts DOS archive (".PEA" family) member method 1.
 *
 * A 12-bit LZW dialect: LSB-first packing, width starts at 9 and is widened
 * by the reader BEFORE each fetch while the current max code is below the
 * next free code (no "early change"), 0x100 = CLEAR, 0x101 = END, first free
 * code 0x102, no byte alignment after a CLEAR.  At width 12 the max code
 * becomes 4096 exactly, so the table stops growing instead of widening again.
 *
 * The stream must open with CLEAR; the code that follows a CLEAR must be a
 * bare literal, because that is what seeds the previous-code register.
 *
 * The container stores the uncompressed size, so no measuring entry point is
 * needed: pass that size as @p output_size.
 */
XXFC_API bool xx_ea_decode_memory(const uint8_t *input, size_t input_size,
                                  uint8_t *output, size_t output_size,
                                  size_t *written);

/**
 * @brief EALIB member method 1.
 *
 * Okumura LZSS: 4 KiB ring, F = 18, THRESHOLD = 3, one LSB-first flag byte
 * per eight tokens, ring cursor initialised to N - F.  The producer clears
 * the whole ring to 0x00 (not to 0x20 as the classic Okumura code does), so a
 * reference into the pre-history yields zero bytes.
 *
 * The container stores the uncompressed size (a four-byte prefix the reader
 * has already consumed), so no measuring entry point is needed: pass that
 * size as @p output_size.  Decoding stops the moment @p output_size bytes
 * exist, which truncates a final match that overshoots -- see the note in the
 * implementation; that is deliberate and matches the reference.
 */
XXFC_API bool xx_ea_lib_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

/**
 * @brief Electronic Arts RefPack (QFS), whole container including the header.
 *
 * @p input must start at the two-byte signature word: flags byte, then the
 * constant 0xFB.  Flag 0x10 is required, 0x01 widens the size fields from
 * three bytes to four and 0x80 adds a packed-size field ahead of the
 * uncompressed-size field; all size fields are BIG-endian.
 *
 * The uncompressed size lives inside the stream rather than in any container
 * record, so a caller that has not parsed the header cannot size its buffer.
 * Use xx_ea_refpack_scan_memory() for that, or simply pass a buffer at least
 * as large as the declared size.  The decode fails if the declared size does
 * not fit in @p output_size.
 */
XXFC_API bool xx_ea_refpack_decode_memory(const uint8_t *input,
                                          size_t input_size, uint8_t *output,
                                          size_t output_size, size_t *written);

/**
 * @brief Measure a RefPack container without materialising it.
 *
 * Walks the same command grammar as the decoder but only counts output bytes,
 * so it is cheap enough to use as a validity probe on arbitrary data.  It
 * succeeds only when the grammar closes on an explicit 0xFC..0xFF terminator
 * and the counted output equals the size declared in the header.
 *
 * @param input       Container bytes, starting at the signature word.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a stream that would decode to more than this.
 * @param consumed    Receives the container length through the terminator.
 * @param produced    Receives the decoded size.  May be NULL.
 */
XXFC_API bool xx_ea_refpack_scan_memory(const uint8_t *input,
                                        size_t input_size, size_t max_output,
                                        size_t *consumed, size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
