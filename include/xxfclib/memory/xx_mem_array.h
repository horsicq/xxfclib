/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_mem_array.h
 * @brief Overflow checked sizes and array allocation.
 *
 * `xx_mem_alloc(count * size)` silently under-allocates when the product
 * wraps. These helpers refuse instead. Unlike xx_mem_alloc, a request of 0
 * bytes gets a (1 byte) block, so NULL always means failure.
 *
 * Moved from CPDBRipper (cpdbr_size_mul / cpdbr_alloc_array /
 * cpdbr_realloc_array) into the library in 2026-11.
 */

#ifndef XXFCLIB_XX_MEM_ARRAY_H
#define XXFCLIB_XX_MEM_ARRAY_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief *out = a * b; false (and *out untouched) when it does not fit a size_t. */
XXFC_API bool xx_size_mul(size_t a, size_t b, size_t *out);

/** @brief *out = a + b; false (and *out untouched) when it does not fit a size_t. */
XXFC_API bool xx_size_add(size_t a, size_t b, size_t *out);

/**
 * @brief xx_mem_alloc of @p count * @p size bytes (not zeroed).
 * @return NULL on overflow or when out of memory; a 1 byte block for 0 bytes.
 *         Free with xx_mem_free.
 */
XXFC_API void *xx_mem_alloc_array(size_t count, size_t size);

/**
 * @brief xx_mem_realloc of @p pointer to @p count * @p size bytes.
 * @return NULL on overflow or when out of memory, and @p pointer is then
 *         still valid and unchanged; a 1 byte block for 0 bytes (never frees).
 */
XXFC_API void *xx_mem_realloc_array(void *pointer, size_t count, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_MEM_ARRAY_H */
