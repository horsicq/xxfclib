/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_QDA_H
#define XXFCLIB_ALGO_QDA_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief QDA byte-pair-encoding (BPE) stream decoder.
 *
 * A stream is a sequence of blocks, each one a pair table followed by a
 * 32-bit little-endian count of packed bytes.  A byte that maps to itself in
 * the table is a literal; any other byte expands to a pair, recursively, via
 * a 256-entry stack.  The QDA container stores the uncompressed size in its
 * directory, so no measuring entry point is provided.
 *
 * @param input       Packed bytes (the whole member stream).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity of @p output -- also the declared member size
 *                    the reference decoder is given.
 * @param written     Receives the produced length (0 on failure).
 * @return true only when every block decoded within the capacity.
 */
XXFC_API bool xx_qda_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif
#endif
