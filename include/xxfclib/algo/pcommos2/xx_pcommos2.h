/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_PCOMMOS2_H
#define XXFCLIB_ALGO_PCOMMOS2_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* IBM Personal Communications for OS/2 (PCOMM 4.x) install-diskette payload.
 *
 * The packed file carries NO header of any kind: the last character of the
 * 8.3 name is replaced by '_' and the body is the raw token stream, so both
 * recognition and the plaintext size only exist once the stream is walked.
 *
 * The stream is a sequence of independent blocks, each producing at most
 * 16384 plaintext bytes.  Match sources never cross a block boundary, which
 * is what lets the encoder spend an absolute 16-bit offset on a far match.
 * Reading a control byte C:
 *   C >= 0xE1  literal run of (C - 0xE0) bytes, 1..31
 *   C == 0xE0  end of the current block; the next block starts empty
 *   C >= 0x20  two-byte token: length (C >> 5) + 2, i.e. 3..8,
 *              distance back = ((C & 0x1F) << 8 | B1) + 1, i.e. 1..8192
 *   C <  0x20  three-byte token: length C + 4, i.e. 4..35, and the source is
 *              the ABSOLUTE offset B1 | (B2 << 8) counted from the start of
 *              the current block -- not a distance back.
 * A complete file ends with two 0xE0 bytes (one closing the last data block,
 * one closing an empty terminator block) and every block before the last data
 * block is exactly 16384 bytes.
 */
#define XX_PCOMMOS2_BLOCK_SIZE 16384U

/**
 * @brief Walk the token stream without producing output and report its size.
 *
 * The container stores no uncompressed size, so this is both the size oracle
 * for the archive record and the detection gate: the block-size and
 * full-consumption invariants it enforces are what keep a headerless format
 * from matching arbitrary binaries.
 *
 * @param input       Packed bytes.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a stream that would decode to more than this.
 * @param consumed    Receives the input bytes used (always @p input_size on
 *                    success -- the stream must end exactly at the buffer
 *                    end).  May be NULL.
 * @param produced    Receives the decoded size.  May be NULL.
 * @return true when the whole buffer decoded as a well-formed block chain.
 */
XXFC_API bool xx_pcommos2_scan_memory(const uint8_t *input, size_t input_size,
                                      size_t max_output, size_t *consumed,
                                      size_t *produced);

/**
 * @brief Decode a PCOMM OS/2 stream.
 *
 * @p output_size must be the value xx_pcommos2_scan_memory() reported; the
 * decode refuses to publish anything that does not reproduce it exactly.
 */
XXFC_API bool xx_pcommos2_decode_memory(const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_size, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
