/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_u64_set.h
 * @brief Hash set of 64 bit keys, and the 64 bit hash it is built on.
 *
 * Open addressing with linear probing, load factor at most 1/2, capacity a
 * power of two. Membership only: there is no removal and no iteration order.
 * A zeroed struct is an empty set (no init call needed).
 *
 * @par Failure handling
 * A failed growth raises the sticky @ref xx_u64_set_t::failed flag (cleared by
 * clear / free); the insert then reports "not added".
 *
 * Moved from CPDBRipper (cpdbr_u64_set, cpdbr_hash_bytes) into the library in
 * 2026-11. The hash values are part of the contract (callers derive names and
 * orders from them) and must not change.
 */

#ifndef XXFCLIB_XX_U64_SET_H
#define XXFCLIB_XX_U64_SET_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_u64_set_s {
    uint64_t *keys;
    uint8_t *used;
    size_t capacity; /**< Power of two, or 0 before the first insert. */
    size_t count;
    bool failed; /**< Sticky: a growth failed since the last clear / free. */
} xx_u64_set_t;

/** @brief Empty set; allocates nothing (same as zeroing the struct). */
XXFC_API void xx_u64_set_init(xx_u64_set_t *set);

/** @brief Release the table and return @p set to the empty state. */
XXFC_API void xx_u64_set_free(xx_u64_set_t *set);

/**
 * @brief Remove every key and clear the failed flag. A large table (more than
 *        4096 slots) is released, a small one kept for reuse.
 */
XXFC_API void xx_u64_set_clear(xx_u64_set_t *set);

XXFC_API bool xx_u64_set_contains(const xx_u64_set_t *set, uint64_t key);

/**
 * @brief Add @p key.
 * @return true when it was added; false when it was present already or the
 *         table could not grow (then @ref xx_u64_set_t::failed is set).
 */
XXFC_API bool xx_u64_set_insert(xx_u64_set_t *set, uint64_t key);

/** @brief Number of keys held. */
XXFC_API size_t xx_u64_set_count(const xx_u64_set_t *set);

/** @brief The 64 bit finaliser of MurmurHash3 (fmix64). */
XXFC_API uint64_t xx_hash_mix64(uint64_t value);

/**
 * @brief 64 bit hash of @p length bytes: FNV-1a started from
 *        (offset basis ^ @p seed), finished with xx_hash_mix64. Not a
 *        cryptographic hash.
 */
XXFC_API uint64_t xx_hash_bytes64(const void *data, size_t length, uint64_t seed);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_U64_SET_H */
