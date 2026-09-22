/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_HZL_H
#define XXFCLIB_ALGO_HZL_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode an HZL (LZHUF) stream of known plaintext length.
 *
 * Haruyasu Yoshizaki / Haruhiko Okumura LZHUF -- LZSS driven by an adaptive
 * Huffman coder -- in the exact sub-variant used by the "!HZL" single-file
 * compressor and by the headerless DOS "JBF" archiver.  It is NOT the same
 * stream as the ARCV LZHUF variants: those carry a 256 stop code
 * (N_CHAR 287/315), start the ring cursor at N - F and use a 4 KiB window.
 * This one has
 *
 *     window N      = 8192  (the ring index is masked with 0x1fff)
 *     max match F   = 60
 *     THRESHOLD     = 2     -> symbol = length + 253
 *     N_CHAR        = 314   (256 literals + 58 length codes, NO stop code)
 *     T = 627, R = 626
 *     ring prefilled with 0x20, ring cursor starts at 0 (not N - F)
 *     position code = classic 6-low-bit table (d_code << 6 | i & 0x3f), i.e.
 *                     distances stay below 4096 even though the ring is 8 KiB
 *
 * Because the stream carries no terminator, decoding always runs to the
 * caller-supplied plaintext length: @p output_size IS that length, and a
 * successful call always produces exactly @p output_size bytes.  Both
 * containers that use this codec (HZL and JBF) store the uncompressed size in
 * their headers, so no measuring entry point is provided.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Plaintext length; exactly this many bytes are produced.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only on a complete decode of @p output_size bytes.
 */
XXFC_API bool xx_hzl_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

#ifdef __cplusplus
}
#endif
#endif
