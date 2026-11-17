/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_byteset.h
 * @brief Find the first byte of a buffer that belongs to a set of byte values.
 *
 * A byte set is a 256-bit membership bitmap plus search tables that
 * xx_byteset_prepare derives from it. xx_byteset_find returns the lowest
 * position at or after a start offset whose byte is a member; it is meant to
 * be called again from the position after each hit. Sets of one or two
 * members (or of all but one to three values) compare every byte with each
 * member, a single range of values is a subtract-and-compare, up to sixteen
 * members with distinct low nibbles are one table lookup and compare (three
 * members that share a low nibble compare with each), and larger sets
 * classify each byte by its two nibbles with byte shuffles: a one-table
 * bitmap when every member is below 0x80, shufti when eight (low nibbles x
 * high nibbles) buckets describe the set exactly, otherwise the exact
 * two-table bitmap (truffle). Every strategy is exact. SSE2/AVX2 backends
 * test 16/32 bytes at once and are chosen at run time; the portable scalar
 * code is the reference they must equal.
 */

#ifndef XX_BYTESET_H
#define XX_BYTESET_H

#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Backend selectors for xx_byteset_find_backend. */
#define XX_BYTESET_BACKEND_SCALAR 0
#define XX_BYTESET_BACKEND_SSE2 1
#define XX_BYTESET_BACKEND_AVX2 2

/** A set of byte values. Plain data: no pointers, no heap, copyable with
 * memcpy. A zero-filled struct is an empty set that is not prepared. */
typedef struct xx_byteset {
    uint64_t bits[4]; /**< membership bitmap, bit b set when byte value b is a member */
    /* Private: written by xx_byteset_prepare, read by the search backends. */
    uint8_t table_low[16];  /**< shufti low-nibble buckets, or truffle rows of bytes below 0x80 */
    uint8_t table_high[16]; /**< shufti high-nibble buckets, or truffle rows of bytes from 0x80 */
    uint8_t values[4];      /**< compared values, or the first value and span of a range */
    uint8_t strategy;       /**< how the set is searched; 0 = not prepared (scalar bitmap) */
    uint8_t reserved[3];    /**< zero; nothing in the set depends on the CPU that prepared it */
} xx_byteset;

/** Make the set empty; an empty set is prepared. */
XXFC_API void xx_byteset_clear(xx_byteset *set);

/** Add a member. The set is unprepared until xx_byteset_prepare runs again
 * (an unprepared set is still searched exactly, by the scalar loop). */
XXFC_API void xx_byteset_add(xx_byteset *set, uint8_t value);

/** Choose the search strategy and build its tables; call after the last add.
 * Calling it again is harmless. */
XXFC_API void xx_byteset_prepare(xx_byteset *set);

/** Number of members, 0..256. */
XXFC_API size_t xx_byteset_count(const xx_byteset *set);

/** True when value is a member. */
XXFC_API bool xx_byteset_contains(const xx_byteset *set, uint8_t value);

/** Lowest i with from <= i < size and data[i] a member; returns size when
 * there is none (also when from >= size). Reads data[from .. size - 1] and,
 * when the search reaches the last vector of the buffer (fewer than one
 * vector of bytes left, at the start or after the aligned steps), up to 31
 * bytes before from that it ignores: that vector is loaded from
 * data + size - 16 (SSE2) or data + size - 32 (AVX2). Never reads before data
 * or at or past data + size. An unprepared set is searched exactly, but
 * slowly. Thread-safe on a const set. */
XXFC_API size_t xx_byteset_find(const xx_byteset *set, const void *data, size_t size, size_t from);

/** True when the backend is compiled in and enabled on this CPU. */
XXFC_API bool xx_byteset_backend_available(int backend);

/** xx_byteset_find with an explicit backend, for differential tests.
 * Returns false and leaves *found untouched when the backend is not
 * available. */
XXFC_API bool xx_byteset_find_backend(int backend, const xx_byteset *set, const void *data, size_t size,
                                      size_t from, size_t *found);

#ifdef __cplusplus
}
#endif

#endif /* XX_BYTESET_H */
