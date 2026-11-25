/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_LZMAT_H
#define XXFCLIB_ALGO_LZMAT_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Decode one nonempty raw LZMAT stream without allocation.
 *
 * No size header is consumed. output_capacity is a ceiling, not an expected
 * size or an early-stop condition. All supplied bytes are interpreted as the
 * stream; unused final control bits/a final half-byte are format padding.
 * LZMAT has no EOS marker, so a valid prefix cannot authenticate a transport
 * trailer. Callers with an external size must compare it with *written.
 *
 * Input/output ranges must not overlap, and written must be a separate valid
 * size_t object. On failure *written is zero; output may contain a partial
 * prefix. On success it is the actual decoded size. No input bytes are changed.
 */
XXFC_API bool xx_lzmat_decompress(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_capacity, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
