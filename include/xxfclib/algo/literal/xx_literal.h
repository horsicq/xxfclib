/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_LITERAL_H
#define XX_LITERAL_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_literal_set xx_literal_set;
typedef bool (*xx_literal_match_fn)(void *context, size_t insertion_index, size_t position);

XXFC_API xx_literal_set *xx_literal_set_create(void);
XXFC_API void xx_literal_set_free(xx_literal_set *set);
/* Copy a 1..8 byte literal. Duplicate literals retain separate insertion indices.
 * Adding after finalize is supported and invalidates the prepared finder. */
XXFC_API bool xx_literal_set_add(xx_literal_set *set, const uint8_t *bytes, size_t length);
XXFC_API bool xx_literal_set_finalize(xx_literal_set *set);
XXFC_API size_t xx_literal_set_count(const xx_literal_set *set);
/* Report matches in increasing start position, ties in insertion order. Only
 * starts in [start_begin, start_end) are reported; all size bytes remain readable
 * for verification across range boundaries. Empty ranges accept NULL data.
 * False means invalid arguments/unfinalized set or a callback requested stop.
 * A finalized set is immutable and may be scanned concurrently. */
XXFC_API bool xx_literal_set_scan(const xx_literal_set *set, const uint8_t *data, size_t size,
                                 size_t start_begin, size_t start_end,
                                 xx_literal_match_fn match, void *context);
/* Independent, straightforward oracle and portable fallback. */
XXFC_API bool xx_literal_set_scan_scalar(const xx_literal_set *set, const uint8_t *data, size_t size,
                                        size_t start_begin, size_t start_end,
                                        xx_literal_match_fn match, void *context);
/* Describes the selected structure (Teddy or prefix hash), not CPU support. */
XXFC_API const char *xx_literal_set_strategy(const xx_literal_set *set);

#ifdef __cplusplus
}
#endif
#endif
