/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_LITERAL_INTERNAL_H
#define XX_LITERAL_INTERNAL_H

#include "xxfclib/algo/literal/xx_literal.h"
#include "xxfclib/memory/xx_memory.h"

#include <string.h>

#define XX_LITERAL_NONE UINT32_MAX
/* Sets up to this size use the bucketed nibble filter with per-bucket item masks. */
#define XX_LITERAL_SMALL_MAX 64
/* The block scanners read this many bytes past the last start of a block,
 * so every candidate of a block can be verified with one 8-byte load. */
#define XX_LITERAL_BLOCK 64
#define XX_LITERAL_BLOCK_SLACK 8
/* Bucket bits of the large-set pair screen (16 per pair, see xx_literal.c). */
#define XX_LITERAL_SCREEN_LEN3 0x003cU
#define XX_LITERAL_SCREEN_LONG 0xffc0U

typedef struct xx_literal_item {
    uint64_t value; /* bytes in native order, zero padded to 8 */
    uint64_t mask;  /* native-order mask of the first length bytes */
    uint32_t next;  /* next (higher) index with the same bytes and length */
    uint8_t bytes[8];
    uint8_t length;
    uint8_t bucket;
} xx_literal_item;

/* Open-addressing slot of the exact table: the lowest insertion index of a
 * distinct (length, bytes) pair; higher duplicates follow items[].next. */
typedef struct xx_literal_slot {
    uint64_t value;
    uint32_t first;
    uint16_t length;
    uint16_t duplicates; /* nonzero when items[first].next continues the chain */
} xx_literal_slot;

struct xx_literal_set {
    xx_literal_item *items;
    size_t count;
    size_t capacity;
    /* Large sets: exact table, two-byte length screen, four-byte fingerprints. */
    xx_literal_slot *slots;
    uint32_t *pair_screen; /* low half: buckets by the pair at offset 0, high: at offset 2 */
    uint8_t *pair_lengths;
    uint64_t *prefix_bloom;
    size_t hash_mask;
    unsigned hash_shift;
    uint64_t length_masks[9];
    uint8_t single_bytes[256];
    /* Nibble masks of the SIMD filter (bucket bits per input byte). */
    uint8_t low[4][16];
    uint8_t high[4][16];
    /* Small sets: items (as bits of insertion index) of every bucket
     * combination, and for crowded combinations the lengths to look up. */
    uint64_t bucket_items[256];
    uint8_t bucket_lengths[256];
    uint8_t filter_bytes;
    bool small;
    bool use_filter;
    bool ssse3;
    bool finalized;
};

bool xx_literal_verify(const xx_literal_set *set, const uint8_t *data, size_t size,
                       size_t position, uint8_t buckets, xx_literal_match_fn match, void *context);
bool xx_literal_scan_prepared(const xx_literal_set *set, const uint8_t *data, size_t size,
                              size_t begin, size_t end, xx_literal_match_fn match, void *context);
bool xx_literal_scan_ssse3(const xx_literal_set *set, const uint8_t *data, size_t size,
                           size_t begin, size_t end, xx_literal_match_fn match, void *context);
bool xx_literal_scan_avx2(const xx_literal_set *set, const uint8_t *data, size_t size,
                          size_t begin, size_t end, xx_literal_match_fn match, void *context);
bool xx_literal_scan_blocks_sse2(const xx_literal_set *set, const uint8_t *data, size_t size,
                                 size_t begin, size_t end, xx_literal_match_fn match, void *context);
bool xx_literal_verify_large_fast(const xx_literal_set *set, const uint8_t *data, size_t position,
                                  uint8_t lengths, xx_literal_match_fn match, void *context);

#if !defined(XX_LITERAL_PORTABLE_ONLY) && !defined(_M_ARM64EC) && (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__))
#define XX_LITERAL_X86 1
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#define XX_LITERAL_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#define XX_LITERAL_INLINE inline __attribute__((always_inline))
#endif
#endif
#ifndef XX_LITERAL_INLINE
#if defined(_MSC_VER)
#define XX_LITERAL_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define XX_LITERAL_INLINE inline __attribute__((always_inline))
#else
#define XX_LITERAL_INLINE inline
#endif
#endif
#if defined(XX_LITERAL_X86) && (defined(__GNUC__) || defined(__clang__))
#define XX_LITERAL_SSE2 __attribute__((target("sse2")))
#define XX_LITERAL_SSSE3 __attribute__((target("ssse3")))
#define XX_LITERAL_AVX2 __attribute__((target("avx2")))
#else
#define XX_LITERAL_SSE2
#define XX_LITERAL_SSSE3
#define XX_LITERAL_AVX2
#endif

static XX_LITERAL_INLINE unsigned xx_literal_first_bit(uint32_t bits)
{
#if defined(_MSC_VER) && defined(XX_LITERAL_X86)
    unsigned long index;
    _BitScanForward(&index, bits);
    return (unsigned)index;
#elif defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctz(bits);
#else
    unsigned index = 0;
    while ((bits & 1U) == 0) { bits >>= 1; ++index; }
    return index;
#endif
}

static XX_LITERAL_INLINE unsigned xx_literal_first_bit64(uint64_t bits)
{
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned long index;
    _BitScanForward64(&index, bits);
    return (unsigned)index;
#elif defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(bits);
#else
    uint32_t low = (uint32_t)bits;
    return low != 0 ? xx_literal_first_bit(low) : 32U + xx_literal_first_bit((uint32_t)(bits >> 32));
#endif
}

/* Native-order unaligned loads; values and masks are built the same way, so
 * the masked compares and table keys do not depend on byte order. */
static XX_LITERAL_INLINE uint64_t xx_literal_load64(const uint8_t *data)
{
    uint64_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

static XX_LITERAL_INLINE unsigned xx_literal_load16(const uint8_t *data)
{
    uint16_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

/* Up to 8 bytes; missing bytes read as zero (callers bound lengths by remaining). */
static XX_LITERAL_INLINE uint64_t xx_literal_load_tail(const uint8_t *data, size_t remaining)
{
    uint8_t bytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    memcpy(bytes, data, remaining < 8 ? remaining : 8);
    return xx_literal_load64(bytes);
}

static XX_LITERAL_INLINE size_t xx_literal_slot_index(const xx_literal_set *set, uint64_t value, unsigned length)
{
    value += (uint64_t)length * UINT64_C(0x9e3779b97f4a7c15);
    value ^= value >> 29;
    return (size_t)((value * UINT64_C(0xbf58476d1ce4e5b9)) >> set->hash_shift);
}

/* Small sets, at least 8 readable bytes at position: every item of the
 * candidate buckets in insertion order, one masked compare each; crowded
 * bucket combinations use the exact table (every item matching here is in
 * a candidate bucket, so looking up their lengths finds the same items). */
static XX_LITERAL_INLINE bool xx_literal_verify_small_fast(const xx_literal_set *set, const uint8_t *data,
                                                           size_t position, uint8_t buckets,
                                                           xx_literal_match_fn match, void *context)
{
    uint64_t items = set->bucket_items[buckets];
    uint64_t word = xx_literal_load64(data + position);
    if (set->bucket_lengths[buckets] != 0)
        return xx_literal_verify_large_fast(set, data, position, set->bucket_lengths[buckets], match, context);
    while (items != 0) {
        unsigned i = xx_literal_first_bit64(items);
        const xx_literal_item *item = &set->items[i];
        items &= items - 1;
        if ((word & item->mask) == item->value && !match(context, i, position)) return false;
    }
    return true;
}

/* Large sets: the four-byte fingerprint screen for lengths >= 4. Needs four
 * readable bytes. */
static XX_LITERAL_INLINE uint8_t xx_literal_screen(const xx_literal_set *set, const uint8_t *data, uint8_t lengths)
{
    if ((lengths & 248U) != 0) {
        unsigned key = xx_literal_load16(data);
        uint64_t bits = (UINT64_C(1) << (data[2] & 31U)) | (UINT64_C(1) << (32U + (data[3] & 31U)));
        if ((set->prefix_bloom[key] & bits) != bits) lengths &= 7U;
    }
    return lengths;
}

/* Lengths to confirm for a start whose pair-screen buckets are nonzero
 * (needs 8 readable bytes). */
static XX_LITERAL_INLINE uint8_t xx_literal_screen_lengths(const xx_literal_set *set, const uint8_t *data, uint32_t buckets)
{
    uint8_t lengths = (uint8_t)(buckets & 3U);
    if ((buckets & XX_LITERAL_SCREEN_LEN3) != 0) lengths |= 4U;
    if ((buckets & XX_LITERAL_SCREEN_LONG) != 0)
        lengths |= xx_literal_screen(set, data, (uint8_t)(set->pair_lengths[xx_literal_load16(data)] & 248U));
    return lengths;
}
#endif
