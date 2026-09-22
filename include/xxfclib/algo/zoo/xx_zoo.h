/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ZOO_H
#define XXFCLIB_ALGO_ZOO_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a ZOO method 1 ("lzd") member.
 *
 * LZD is Rahul Dhesi's variable-width LZW: codes are packed LSB-first, start
 * at 9 bits and grow to 13, code 256 clears the table and code 257 is an
 * explicit end marker. It is not the same packing as the LZW in TIFF/PDF
 * (MSB-first, early change) nor the same as UNIX compress (no clear-required
 * prologue), so it gets its own decoder.
 *
 * Every ZOO directory entry stores the original size, so the decoded length is
 * always known up front and there is no scanning entry point: @p output_size
 * is the exact expected size and a stream that produces anything else fails.
 *
 * @param input       Compressed bytes of one member.
 * @param input_size  Length of @p input; the stream must end exactly here.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected decoded size.
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true only when the stream decoded completely to its end marker and
 *         produced exactly @p output_size bytes.
 */
XXFC_API bool xx_zoo_lzd_decode_memory(const uint8_t *input, size_t input_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written);

/**
 * @brief Decode a ZOO method 2 ("lzh") member.
 *
 * ZOO's method 2 is the LHA -lh5- algorithm (both descend from Haruyasu
 * Yoshizaki's ar002): block-structured Huffman over an 8 KiB window, with the
 * same pre-table, literal table and position table layout. This therefore
 * forwards to xx_lzh5_decode_memory() with method 5 rather than duplicating
 * it; see the comment at the definition for what ZOO adds on top (an explicit
 * zero-sized terminating block, which only matters when the decoded size is
 * not known -- and in ZOO it always is).
 *
 * @param input       Compressed bytes of one member.
 * @param input_size  Length of @p input.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected decoded size.
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_zoo_lzh_decode_memory(const uint8_t *input, size_t input_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written);

#ifdef __cplusplus
}
#endif
#endif
