/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_CHIEFLZ_H
#define XXFCLIB_ALGO_CHIEFLZ_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ChiefLZ method 4: MSB-first adaptive Huffman plus LZ77.
 *
 * The probability model is the order-0 adaptive Huffman that ARCV v4 method 2
 * uses, but with a 629 symbol alphabet (256 literals, an end marker at 256,
 * then six distance buckets of sixty-two lengths each) and with the bits
 * arriving MSB-first out of a little-endian 16 bit word rather than LSB-first
 * out of single bytes.
 *
 * Both ChiefLZ containers store the plaintext length in their header, so no
 * measuring entry point is provided: @p output_size is the exact decoded size.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Exact expected decoded size.
 * @param written     Receives the number of bytes produced (0 on failure).
 * @return true only when exactly @p output_size bytes were decoded.
 */
XXFC_API bool xx_chieflz_decode_memory(const uint8_t *input, size_t input_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written);

#ifdef __cplusplus
}
#endif

#endif
