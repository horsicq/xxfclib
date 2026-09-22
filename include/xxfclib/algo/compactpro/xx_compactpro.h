/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_COMPACTPRO_H
#define XXFCLIB_ALGO_COMPACTPRO_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Block size Compact Pro (.cpt) archives use for their LZH stream. */
#define XX_COMPACTPRO_BLOCK_SIZE 0x1fff0U
/* Block size DiskDoubler uses for the same LZH stream. */
#define XX_COMPACTPRO_DD_BLOCK_SIZE 0xfff0U

/**
 * @brief Decode a Compact Pro "RLE only" member.
 *
 * The member is the Compact Pro byte-level RLE scheme applied directly to the
 * stored bytes, with no Huffman layer.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected plaintext size (the container stores it).
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_compactpro_rle_decode_memory(const uint8_t *input,
                                              size_t input_size,
                                              uint8_t *output,
                                              size_t output_size,
                                              size_t *written);

/**
 * @brief Decode a Compact Pro "LZH" member.
 *
 * Same RLE scheme, but fed from an LZH stream (block-structured Huffman over
 * an 8 KiB window) instead of from the stored bytes. Uses the .cpt block size.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected plaintext size.
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_compactpro_lzh_decode_memory(const uint8_t *input,
                                              size_t input_size,
                                              uint8_t *output,
                                              size_t output_size,
                                              size_t *written);

/**
 * @brief Decode either Compact Pro form with an explicit block size.
 *
 * DiskDoubler embeds the very same codec but with a different LZH block size,
 * so it can be served from here rather than from a second copy of the code.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param lzh         true for the LZH form, false for the RLE-only form.
 * @param block_size  LZH block size; ignored when @p lzh is false.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected plaintext size.
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_compactpro_decode_memory(const uint8_t *input,
                                          size_t input_size, bool lzh,
                                          uint32_t block_size,
                                          uint8_t *output, size_t output_size,
                                          size_t *written);

#ifdef __cplusplus
}
#endif
#endif
