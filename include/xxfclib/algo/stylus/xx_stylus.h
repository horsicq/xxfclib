/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_STYLUS_H
#define XXFCLIB_ALGO_STYLUS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a Stylus "DP"/SDC dictionary stream.
 *
 * XOR-0xB5 obfuscated LZSS over a zero-filled 4 KiB ring with a +18 match
 * bias.  The container stores no uncompressed size, so a caller normally runs
 * xx_stylus_scan_memory() first and then decodes into a buffer of exactly the
 * measured size; this entry point succeeds only when the stream fills
 * @p output_size exactly.
 *
 * @param input       Compressed (still XORed) bytes of the member.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity, and the exact expected plaintext length.
 * @param written     Receives the produced length.  Set on every path.
 * @return true on a complete decode that filled @p output_size exactly.
 */
XXFC_API bool xx_stylus_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

/**
 * @brief Measure a Stylus stream without knowing its decoded size.
 *
 * Runs the same core through a sliding window instead of a caller buffer, so
 * the measurement and the decode can never disagree.  The format has no end
 * marker: the stream ends when the input runs out, so @p consumed is always
 * @p input_size on success.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a stream that would decode to more than this.
 * @param consumed    Receives the input bytes used.  May be NULL.
 * @param produced    Receives the decoded size.  May be NULL.
 * @return true when the whole stream decoded within the limit.
 */
XXFC_API bool xx_stylus_scan_memory(const uint8_t *input, size_t input_size,
                                    size_t max_output, size_t *consumed,
                                    size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
