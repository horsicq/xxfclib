/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_COKTELLZ_H
#define XXFCLIB_ALGO_COKTELLZ_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a Coktel Vision STK/ITK LZSS stream (classic Okumura LZSS).
 *
 * 4096 byte window initialised to 0x20 (space), initial write position 4078
 * (N - F), match offsets carry no bias.  @p input is the raw LZSS data: the
 * caller strips the STK member's 4-byte uncompressed-size prefix and passes
 * that size as @p output_size.
 *
 * The declared size is authoritative: the stream may end mid-match and the
 * last flag byte may carry unused bits, so decoding stops by output count,
 * not by input EOF.  Input EOF before the declared size is reached is a
 * failure.
 *
 * @param input       Raw LZSS bytes.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity of @p output, and the declared decoded size.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_coktellz_decode_memory(const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_size, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
