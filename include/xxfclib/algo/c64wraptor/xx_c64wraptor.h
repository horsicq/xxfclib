/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_C64WRAPTOR_H
#define XXFCLIB_ALGO_C64WRAPTOR_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a Commodore 64 "Wraptor" LZSS member stream.
 *
 * MSB-first bits over a 4096 byte ring buffer that starts ZERO filled (early
 * matches legitimately copy those zeros, so the initial content is part of the
 * format).  The offset field starts 8 bits wide and widens during the stream.
 * The stream ends only at an explicit end escape; running out of input is a
 * failure, never a clean end.
 *
 * @param input       Packed bytes (the member stream, starting at its first bit).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity of @p output, and the expected decoded size.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only when the stream reached its end escape having produced
 *         exactly @p output_size bytes.
 */
XXFC_API bool xx_c64wraptor_decode_memory(const uint8_t *input,
                                          size_t input_size, uint8_t *output,
                                          size_t output_size, size_t *written);

/**
 * @brief Measure a Wraptor stream without keeping the output.
 *
 * The container stores neither the member length nor the packed length, so a
 * reader has to walk the stream to find both -- @p consumed is where the
 * member's trailing checksum begins.  Uses the same core routine as the
 * decoder, so the measurement and the decode can never disagree.
 *
 * @param input       Packed bytes.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a stream decoding to more than this.
 * @param consumed    Receives the packed bytes occupied. May be NULL.
 * @param produced    Receives the decoded size. May be NULL.
 * @return true when the stream reached its end escape within the limit.
 */
XXFC_API bool xx_c64wraptor_scan_memory(const uint8_t *input,
                                        size_t input_size, size_t max_output,
                                        size_t *consumed, size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
