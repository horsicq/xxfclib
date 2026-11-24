/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_arena.h
 * @brief Bump allocator: many small blocks, released all at once.
 *
 * Every block comes from a chunk (256 KiB by default; a larger request gets a
 * chunk of its own size). There is no per-block free: @ref xx_arena_reset
 * drops everything but keeps the first chunk for reuse, @ref xx_arena_free
 * releases all chunks.
 *
 * Blocks are zero filled and 8 byte aligned. A request of 0 bytes still gets
 * a distinct block (8 bytes), so a NULL result always means failure.
 *
 * @par Failure handling
 * As in xx_buf_t, a failed allocation raises the sticky @ref xx_arena_t::failed
 * flag (cleared by init, reset and free) and returns NULL, so a caller may
 * allocate a whole structure and check once at the end.
 *
 * Moved from CPDBRipper (cpdbr_arena) into the library in 2026-11.
 */

#ifndef XXFCLIB_XX_ARENA_H
#define XXFCLIB_XX_ARENA_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default chunk size of an arena (256 KiB). */
#define XX_ARENA_DEFAULT_CHUNK_SIZE ((size_t)256 * 1024)

typedef struct xx_arena_chunk_s xx_arena_chunk;

/**
 * @brief Bump allocator. Initialise with @ref xx_arena_init (or zero it and
 *        the default chunk size is used).
 */
typedef struct xx_arena_s {
    xx_arena_chunk *head;    /**< First chunk; kept by reset. */
    xx_arena_chunk *current; /**< Chunk allocations come from. */
    size_t chunk_size;       /**< Bytes per chunk; 0 = XX_ARENA_DEFAULT_CHUNK_SIZE. */
    bool failed;             /**< Sticky: an allocation failed since init / reset. */
} xx_arena_t;

/** @brief Empty arena with the default chunk size. Allocates nothing. */
XXFC_API void xx_arena_init(xx_arena_t *arena);

/** @brief Empty arena with chunks of @p chunk_size bytes (0 = default). */
XXFC_API void xx_arena_init_ex(xx_arena_t *arena, size_t chunk_size);

/**
 * @brief Forget every block. The first chunk is kept for the next
 *        allocations, the others are released; the failed flag is cleared.
 */
XXFC_API void xx_arena_reset(xx_arena_t *arena);

/** @brief Release every chunk and return @p arena to the empty state. */
XXFC_API void xx_arena_free(xx_arena_t *arena);

/**
 * @brief Zero filled, 8 byte aligned block of @p size bytes.
 * @return NULL (and the failed flag raised) when out of memory.
 */
XXFC_API void *xx_arena_alloc(xx_arena_t *arena, size_t size);

/** @brief xx_arena_alloc of @p count * @p size bytes; NULL on overflow. */
XXFC_API void *xx_arena_alloc_array(xx_arena_t *arena, size_t count, size_t size);

/** @brief Copy of @p length bytes of @p text plus a terminating zero. */
XXFC_API char *xx_arena_strndup(xx_arena_t *arena, const char *text, size_t length);

/** @brief Copy of a C string (NULL copies as ""). */
XXFC_API char *xx_arena_strdup(xx_arena_t *arena, const char *text);

/** @brief False once an allocation has failed since init / reset. */
XXFC_API bool xx_arena_ok(const xx_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_ARENA_H */
