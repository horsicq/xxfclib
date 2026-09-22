/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_SLS_H
#define XXFCLIB_ALGO_SLS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode an SLS (LZHUF dialect) stream of known plaintext length.
 *
 * Haruhiko Okumura LZHUF -- LZSS driven by an adaptive Huffman coder -- in the
 * sub-variant used by the SLS installer archives:
 *
 *     window N      = 8192  (ring index masked with 0x1fff)
 *     max match F   = 90
 *     THRESHOLD     = 2     -> symbol = length + 253
 *     N_CHAR        = 344   (256 literals + 88 length codes, NO stop code)
 *     T = 687, R = 686, MAX_FREQ = 0x8000
 *     ring prefilled with 0x20, ring cursor starts at 0 (not N - F)
 *     position code = 13 BIT: (d_code[b] << 7) | (b & 0x7f), with
 *                     d_len[b >> 4] - 1 extra bits shifted into b
 *
 * That 13-bit position field is what separates it from every other LZHUF
 * dialect in this library: xx_hzl uses F = 60 and the classic 6-low-bit
 * position code, so it decodes this family into garbage.
 *
 * The stream carries no terminator, so decoding always runs to the
 * caller-supplied plaintext length: @p output_size IS that length and a
 * successful call always produces exactly @p output_size bytes.  The SLS
 * container stores the uncompressed size, so no measuring entry point is
 * provided.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Plaintext length; exactly this many bytes are produced.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only on a complete decode of @p output_size bytes.
 */
XXFC_API bool xx_sls_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif
#endif
