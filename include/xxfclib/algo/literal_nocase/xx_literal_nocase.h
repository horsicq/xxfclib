/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_LITERAL_NOCASE_H
#define XX_LITERAL_NOCASE_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Find the first ASCII case-insensitive literal at or after start.
 * Only A-Z/a-z compare case-insensitively; all other bytes compare exactly.
 * The needle may contain NULs, including interleaved wide-string NULs.
 * Returns SIZE_MAX for no match, an empty needle or invalid arguments.
 * Does not read outside either supplied buffer. This call is synchronous;
 * callers needing cancellation should search bounded, overlapping ranges.
 * Dispatch respects xxfclib's SSE2/AVX2 runtime feature switches.
 */
XXFC_API size_t xx_literal_nocase_find(const uint8_t *data, size_t size,
                                      size_t start, const uint8_t *needle,
                                      size_t length);

/** Portable reference implementation with the same contract. */
XXFC_API size_t xx_literal_nocase_find_scalar(const uint8_t *data, size_t size,
                                             size_t start,
                                             const uint8_t *needle,
                                             size_t length);

#ifdef __cplusplus
}
#endif
#endif /* XX_LITERAL_NOCASE_H */
