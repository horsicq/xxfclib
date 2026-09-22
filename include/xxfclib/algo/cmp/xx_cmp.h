/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_cmp.h
 * @brief The two codecs of the single-member ".CMP" container (header 0x007F).
 *
 * Ported from XArchive's XCMPDecoder / XSharedLZWDecoder.
 *
 * Method 1 (CMP_LZW) frames the payload: a 16-bit little-endian byte count,
 * then that many bytes, repeated until the member is consumed.  A frame whose
 * count reaches the 4096-byte block size is a STORED block and is copied out
 * whole; a shorter frame is a self-contained 16-bit-max, MSB-first LZW stream
 * with clear code 0x100 and end code 0x101.  Each frame restarts the
 * dictionary.
 *
 * Method 2 (CMP_LZSS) is an LZSS over a 2048-byte ring driven by 9-bit tokens
 * read MSB-first.
 *
 * The container stores the plaintext length in its header (word 0x37 is the
 * block size, dword 0x39 the uncompressed size), so no measuring entry point
 * is required: the caller always knows @p output_size up front.
 */

#ifndef XXFCLIB_ALGO_CMP_H
#define XXFCLIB_ALGO_CMP_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a CMP method 1 (framed LZW / stored) member.
 *
 * @param input       The whole compressed member, starting at the first frame
 *                    length word.
 * @param input_size  Length of @p input.
 * @param output      Destination, exactly @p output_size bytes.
 * @param output_size The plaintext length taken from the container header.
 * @param written     Receives the produced byte count.  Set on every path.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_cmp_lzw_decode_memory(const uint8_t *input,
                                       size_t input_size, uint8_t *output,
                                       size_t output_size, size_t *written);

/**
 * @brief Decode a CMP method 2 (9-bit LZSS) member.
 *
 * Note that the newer container variant does not use this codec at all: it
 * stores a PKWARE DCL stream, which the reader routes to xx_dcl instead.
 *
 * @param input       The whole compressed member.
 * @param input_size  Length of @p input.
 * @param output      Destination, exactly @p output_size bytes.
 * @param output_size The plaintext length taken from the container header.
 * @param written     Receives the produced byte count.  Set on every path.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_cmp_lzss_decode_memory(const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_size, size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_CMP_H */
