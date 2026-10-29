/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_softronics.h @brief Softronics v2.00 LZW decoder. */

#ifndef XXFCLIB_ALGO_SOFTRONICS_H
#define XXFCLIB_ALGO_SOFTRONICS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decode the LSB-first, 9--12 bit GIF-style LZW stream used by Softronics
 * Compressed File v2.00 containers.  The stream must begin with CLEAR (256),
 * finish with END (257), and produce exactly @p output_size bytes.
 *
 * @param consumed_size receives the number of packed bytes consumed on
 * success; it may be NULL.
 */
XXFC_API bool xx_softronics_lzw_decompress_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *consumed_size);
/** Same dialect, but END must occupy the final byte (only bit padding allowed). */
XXFC_API bool xx_softronics_lzw_decompress_memory_exact(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size);

/** Return true to continue. Bounded passes poll before/after parsing and at
 * most every 256 charged code, prefix, output/count or tail operations. */
typedef bool (*xx_softronics_poll_fn)(void *context);
/** Measure with the exact dialect, without a full output buffer. The validated
 * phrase stack still contains bounded scratch bytes. On failure measured_size
 * is zero; reaching output_limit alone never succeeds without END/tail checks.
 * Callers must also bound packed input and reserve codec workspace. */
XXFC_API bool xx_softronics_lzw_measure_memory_exact_bounded(const uint8_t *input, size_t input_size, size_t output_limit,
    uint64_t work_limit, xx_softronics_poll_fn poll, void *context, size_t *measured_size);
/** Exact decoding with the same work/poll rules. work_limit must be nonzero. */
XXFC_API bool xx_softronics_lzw_decompress_memory_exact_bounded(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size,
    uint64_t work_limit, xx_softronics_poll_fn poll, void *context);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_SOFTRONICS_H */
