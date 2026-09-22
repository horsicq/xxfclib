/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_HA_H
#define XXFCLIB_ALGO_HA_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HA method 1, "ASC".
 *
 * LZ77 over a 31200-byte circular window, entropy coded with a
 * Witten-Neal-Cleary arithmetic coder driven by five implicit binary
 * cumulative-frequency trees (seen/unseen characters, seen/unseen lengths,
 * positions) plus a four-state binary literal/match context.
 *
 * The HA member header stores the plaintext size, so @p output_size is the
 * exact decoded length and no measuring entry point is provided.
 *
 * @param input       Compressed bytes (the member payload).
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Exact expected decoded size.
 * @param written     Receives the number of bytes produced (0 on failure).
 * @return true only when exactly @p output_size bytes were decoded.
 */
XXFC_API bool xx_ha_asc_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

/**
 * @brief HA method 2, "HSC".
 *
 * Order-4 PPM with escape coding over the same arithmetic coder: a fixed pool
 * of 10000 context nodes addressed by a 16384-entry hash of the last 1..4
 * bytes, LRU recycling of contexts and frequency-merging of the symbol pool.
 *
 * Same length contract as xx_ha_asc_decode_memory().
 */
XXFC_API bool xx_ha_hsc_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

/**
 * @brief Dispatch on the raw HA method nibble (1 = ASC, 2 = HSC).
 *
 * Method 0 (stored) is not handled here; the reader copies those itself.
 */
XXFC_API bool xx_ha_decode_memory_method(unsigned method, const uint8_t *input,
                                         size_t input_size, uint8_t *output,
                                         size_t output_size, size_t *written);

#ifdef __cplusplus
}
#endif

#endif
