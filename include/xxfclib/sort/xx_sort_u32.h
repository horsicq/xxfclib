/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_sort_u32.h
 * @brief Sorted arrays of 32 bit values: sort + dedupe, binary search.
 *
 * Typed helpers that need no comparator call per element; the result of
 * xx_sort_u32_unique is fully determined (ascending, distinct), whatever the
 * algorithm. Use an xx_list_t of uint32_t (or a plain array) as storage.
 *
 * Moved from CPDBRipper (cpdbr_u32_vec_sort_unique / cpdbr_u32_sorted_contains)
 * into the library in 2026-11.
 */

#ifndef XXFCLIB_XX_SORT_U32_H
#define XXFCLIB_XX_SORT_U32_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sort ascending in place (heap sort: no allocation, no recursion). */
XXFC_API void xx_sort_u32(uint32_t *items, size_t count);

/**
 * @brief Sort ascending and drop duplicates in place.
 * @return The new count (the distinct values are items[0 .. result - 1]).
 */
XXFC_API size_t xx_sort_u32_unique(uint32_t *items, size_t count);

/** @brief Index of the first element >= @p value of an ascending array (count if none). */
XXFC_API size_t xx_sorted_u32_lower_bound(const uint32_t *items, size_t count, uint32_t value);

/** @brief Whether an ascending array holds @p value. */
XXFC_API bool xx_sorted_u32_contains(const uint32_t *items, size_t count, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_SORT_U32_H */
