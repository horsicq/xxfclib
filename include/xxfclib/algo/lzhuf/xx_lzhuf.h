/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_LZHUF_H
#define XXFCLIB_ALGO_LZHUF_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Haruyasu Yoshizaki's LZHUF - LZSS over an ADAPTIVE (dynamic) Huffman tree -
 * as carried by the DOS-era families that embedded it verbatim.
 *
 * This is NOT the LHA family in src/algo/lzh.  xx_lzh1_decode_memory() is the
 * same lineage and almost the same parameters, but it is hard-wired to LHA's
 * -lh1- shape: a 4096-byte ring whose first N-F bytes only are preset to ' ',
 * a write cursor starting at N-F, and a strict "the stream must end within
 * seven bits of the last input byte" test.  The families below want an
 * 8192-byte ring preset to 0x20 throughout with the cursor at 0, and tolerate
 * trailing bytes, so -lh1- cannot decode them and is left alone.
 *
 * Bits are consumed MSB first from a 16-bit window of a 32-bit shift register;
 * the alphabet is 0x00..0xFF plus one symbol per match length; every leaf
 * starts at frequency one and is re-weighted after every symbol; a distance is
 * one byte run through LHA's d_code[256]/d_len[16] position tables plus
 * d_len[byte>>4]-2 further raw bits.
 */

/**
 * @brief Decode a plain (unframed) Yoshizaki LZHUF stream.
 *
 * The parameter set is the one every family here shares: distance variant 1
 * (code<<6 | low six bits, d_len-2 extra bits), F = 0x3C, THRESHOLD = 2 so
 * N_CHAR = 314 (T = 627, R = 626), MAX_FREQ 0x8000 with the textbook halve-and-
 * rebuild, an 0x2000-byte ring preset to 0x20 with the write cursor at 0, no
 * end symbol, length = symbol - 0xFF + 2 and source = (cursor - distance - 1).
 *
 * There is no end symbol, so @p output_size is the ONLY stop condition - which
 * is why there is no measuring entry point for this one: every container that
 * carries it stores the decoded length. The flip side is that a WRONG length is
 * not detectable: asking for fewer bytes than the member holds decodes exactly
 * that many and reports success, because that is the same request as a shorter
 * member. Pass the length the container stores, not a guess.
 *
 * Feed it a BWCF member's payload, i.e. the block WITHOUT its leading four-byte
 * repeat of the uncompressed size.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected size; decoding stops there.
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_lzhuf_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

/**
 * @brief Decode a ZTC (Zortech C / Symantec C++ distribution archive) member.
 *
 * Same codec and same parameters as xx_lzhuf_decode_memory(), but the member
 * payload is NOT a flat stream: it is PAGED, with the page sums inline. Each
 * page is min(0x1000, remaining - 4) bytes followed by a four-byte little
 * endian plain sum of just those bytes, where `remaining` starts at the payload
 * size and loses the four check bytes BEFORE the page length is chosen - which
 * is what makes the last page short by exactly four. Handing the raw payload to
 * xx_lzhuf_decode_memory() decodes the first 4096 bytes correctly and then
 * garbage, so a short member hides the bug entirely.
 *
 * Every page's sum is verified before a single bit is decoded, including pages
 * past the point the codec stops reading.
 *
 * @param input       The member's PAGED payload, exactly as stored.
 * @param input_size  Length of @p input.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected size, from the member header.
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_lzhuf_ztc_decode_memory(const uint8_t *input,
                                         size_t input_size, uint8_t *output,
                                         size_t output_size, size_t *written);

/*
 * The "LZ FF 00" single-file container (XLZHCXP) is named after this family but
 * is NOT a member of it: its payload is LZW, not LZSS-plus-adaptive-Huffman,
 * and it shares no code with the two entry points above. It lives here only so
 * the three readers that were ported together stay in one place; moving it to
 * its own module would change nothing but the file name.
 *
 * The payload is a chain of BLOCKS - one length byte then that many bytes of
 * bit stream, a zero length ends the file - and the framing is invisible to the
 * codec, which is why a reader that starts at a fixed offset decodes garbage.
 * Codes are LSB-first, 10 bits widening to at most 12, checked BEFORE each
 * read against the CURRENT next-free code (no "early change" fudge):
 *
 *   0x000..0x0FF  literals
 *   0x100..0x1FF  never emitted; meeting one is a hard error
 *   0x200         CLEAR - reset width and next-free, then the code that follows
 *                 is emitted directly and becomes the new previous code
 *   0x201         END
 *   0x202..0xFFF  dictionary entries; the table stops growing at 0x1000 rather
 *                 than forcing a reset
 */

/**
 * @brief Decode an XLZHCXP payload.
 *
 * @param input       Bytes from offset 2 of the container, i.e. starting at the
 *                    first block-length byte.
 * @param input_size  Length of @p input.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected size, as learned from xx_lzhuf_lzhcxp_scan_memory().
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_lzhuf_lzhcxp_decode_memory(const uint8_t *input,
                                            size_t input_size, uint8_t *output,
                                            size_t output_size,
                                            size_t *written);

/**
 * @brief Measure an XLZHCXP payload, which stores no uncompressed size.
 *
 * The container is two bytes of "LZ" magic and nothing else - no length, no
 * checksum - so a reader cannot allocate, and cannot even validate, without
 * decoding. This runs the same core with the output discarded.
 *
 * Note that running out of input is the ORDINARY way these streams stop, so a
 * truncated stream measures successfully; that is what the reference does and
 * what its detection path relies on. @p consumed is the input offset the reader
 * stopped at, which a caller can require to be the whole payload - but a stream
 * that ended on the END code stops one byte SHORT of the payload, because the
 * trailing zero-length block marker is never reached.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a stream that would decode to more than this.
 * @param consumed    Receives the input bytes the stream occupies. May be NULL.
 * @param produced    Receives the decoded size. May be NULL.
 * @return true when the stream decoded to a stop within the limit.
 */
XXFC_API bool xx_lzhuf_lzhcxp_scan_memory(const uint8_t *input,
                                          size_t input_size, size_t max_output,
                                          size_t *consumed, size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
