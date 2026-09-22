/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_DCL_H
#define XXFCLIB_ALGO_DCL_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PKWARE Data Compression Library (DCL) stream decoder.  This is the
 * standalone explode format, distinct from ZIP method 6. */
XXFC_API bool xx_dcl_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written);

/**
 * @brief Measure a DCL stream without knowing its decoded size.
 *
 * Runs the same decoder but keeps only a sliding window, so a container that
 * stores no uncompressed size can still learn one -- and, just as usefully,
 * learn exactly how many input bytes the stream occupies. A reader whose
 * format has no magic can then require the stream to end precisely at the end
 * of the file, which is a far stronger test than any header check.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a stream that would decode to more than this.
 * @param consumed    Receives the input bytes the stream occupies. May be NULL.
 * @param produced    Receives the decoded size. May be NULL.
 * @return true when the stream decoded to its end marker within the limit.
 */
XXFC_API bool xx_dcl_scan_memory(const uint8_t *input, size_t input_size,
                                 size_t max_output, size_t *consumed,
                                 size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
