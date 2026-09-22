/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_PAKLEO_H
#define XXFCLIB_ALGO_PAKLEO_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LEOLZW -- the codec of PAKLEO (.PLL), "LEOLZW - (c) Leonardus Leonardi
 * 1993".  Classic LZW over an 8-bit alphabet, codes assembled MSB first,
 * initial width 9, first free code 0x103, table capacity 0x8000.
 *
 *   0x100  end of stream
 *   0x101  WIDEN by one bit, capped at 15.  The width NEVER moves implicitly
 *          with the free-code counter, only when this code appears.
 *   0x102  clear: free code back to 0x103, width back to 9, and the next code
 *          is read at 9 bits and must be a plain literal.
 *
 * PAKLEO records store the uncompressed size, so no measuring entry point is
 * needed: the reader passes it as @p output_size.
 */

/**
 * @brief Decode one LEOLZW member.
 *
 * @param input       Compressed bytes of the member.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size The member's stored uncompressed size, and the capacity.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_pakleo_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

#ifdef __cplusplus
}
#endif
#endif
