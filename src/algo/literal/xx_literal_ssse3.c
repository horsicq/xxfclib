/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_literal_internal.h"

#ifdef XX_LITERAL_X86
/* Nibble screen of 16 starts over filter_bytes leading bytes (a constant
 * after inlining): bucket bits per start, zero for rejected starts. */
#define XX_LITERAL_SSSE3_STEP(j)                                                                     \
    do {                                                                                             \
        __m128i bytes = _mm_loadu_si128((const __m128i *)(const void *)(data + position + (j)));    \
        __m128i lo = _mm_and_si128(bytes, nibbles);                                                  \
        __m128i hi = _mm_and_si128(_mm_srli_epi16(bytes, 4), nibbles);                               \
        candidates = _mm_and_si128(candidates, _mm_and_si128(_mm_shuffle_epi8(low[j], lo),          \
                                                             _mm_shuffle_epi8(high[j], hi)));       \
    } while (0)

XX_LITERAL_SSSE3
static XX_LITERAL_INLINE bool xx_literal_ssse3_loop(const xx_literal_set *set, const uint8_t *data, size_t size,
                                                    size_t *cursor, size_t end, unsigned filter_bytes,
                                                    xx_literal_match_fn match, void *context)
{
    __m128i low[4], high[4];
    const __m128i nibbles = _mm_set1_epi8(15);
    const __m128i zero = _mm_setzero_si128();
    size_t position = *cursor;
    unsigned j;
    for (j = 0; j < filter_bytes; ++j) {
        low[j] = _mm_loadu_si128((const __m128i *)(const void *)set->low[j]);
        high[j] = _mm_loadu_si128((const __m128i *)(const void *)set->high[j]);
    }
    /* 16 starts plus 8 readable bytes behind the last one for confirmation. */
    while (end - position >= 16 && size - position >= 16U + XX_LITERAL_BLOCK_SLACK) {
        __m128i candidates = _mm_set1_epi8((char)255);
        uint32_t bits;
        XX_LITERAL_SSSE3_STEP(0);
        if (filter_bytes > 1) XX_LITERAL_SSSE3_STEP(1);
        if (filter_bytes > 2) XX_LITERAL_SSSE3_STEP(2);
        if (filter_bytes > 3) XX_LITERAL_SSSE3_STEP(3);
        bits = (uint32_t)(~_mm_movemask_epi8(_mm_cmpeq_epi8(candidates, zero))) & UINT32_C(0xffff);
        if (bits != 0) {
            uint8_t buckets[16];
            _mm_storeu_si128((__m128i *)(void *)buckets, candidates);
            do {
                unsigned lane = xx_literal_first_bit(bits);
                size_t at = position + lane;
                bits &= bits - 1;
                if (set->small) {
                    if (!xx_literal_verify_small_fast(set, data, at, buckets[lane], match, context)) return false;
                } else {
                    uint8_t lengths = xx_literal_screen(set, data + at, set->pair_lengths[xx_literal_load16(data + at)]);
                    if (lengths != 0 && !xx_literal_verify_large_fast(set, data, at, lengths, match, context)) return false;
                }
            } while (bits != 0);
        }
        position += 16;
    }
    *cursor = position;
    return true;
}
#endif

XX_LITERAL_SSSE3
bool xx_literal_scan_ssse3(const xx_literal_set *set, const uint8_t *data, size_t size,
                           size_t begin, size_t end, xx_literal_match_fn match, void *context)
{
#ifdef XX_LITERAL_X86
    size_t position = begin;
    bool ok;
    switch (set->filter_bytes) {
    case 1: ok = xx_literal_ssse3_loop(set, data, size, &position, end, 1, match, context); break;
    case 2: ok = xx_literal_ssse3_loop(set, data, size, &position, end, 2, match, context); break;
    case 3: ok = xx_literal_ssse3_loop(set, data, size, &position, end, 3, match, context); break;
    default: ok = xx_literal_ssse3_loop(set, data, size, &position, end, 4, match, context); break;
    }
    if (!ok) return false;
    return xx_literal_scan_prepared(set, data, size, position, end, match, context);
#else
    return xx_literal_scan_prepared(set, data, size, begin, end, match, context);
#endif
}

#ifdef XX_LITERAL_X86
/* Zero-mask of 16 starts: pairs[k] & (pairs[k + 2] >> 16) == 0. The packs
 * saturate, so nonzero stays nonzero. */
XX_LITERAL_SSE2
static XX_LITERAL_INLINE uint32_t xx_literal_blocks_zero16(const uint32_t *pairs, __m128i zero)
{
    __m128i c0 = _mm_and_si128(_mm_loadu_si128((const __m128i *)(const void *)pairs),
                               _mm_srli_epi32(_mm_loadu_si128((const __m128i *)(const void *)(pairs + 2)), 16));
    __m128i c1 = _mm_and_si128(_mm_loadu_si128((const __m128i *)(const void *)(pairs + 4)),
                               _mm_srli_epi32(_mm_loadu_si128((const __m128i *)(const void *)(pairs + 6)), 16));
    __m128i c2 = _mm_and_si128(_mm_loadu_si128((const __m128i *)(const void *)(pairs + 8)),
                               _mm_srli_epi32(_mm_loadu_si128((const __m128i *)(const void *)(pairs + 10)), 16));
    __m128i c3 = _mm_and_si128(_mm_loadu_si128((const __m128i *)(const void *)(pairs + 12)),
                               _mm_srli_epi32(_mm_loadu_si128((const __m128i *)(const void *)(pairs + 14)), 16));
    __m128i bytes = _mm_packs_epi16(_mm_packs_epi32(c0, c1), _mm_packs_epi32(c2, c3));
    return (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, zero));
}
#endif

#ifdef XX_LITERAL_X86
/* Screen entries of the 64 starts of a block and the two pairs behind it. */
static XX_LITERAL_INLINE void xx_literal_blocks_fill(const uint32_t *screen, const uint8_t *block, uint32_t *pairs)
{
    unsigned i;
    for (i = 0; i < XX_LITERAL_BLOCK; i += 4) {
        pairs[i] = screen[xx_literal_load16(block + i)];
        pairs[i + 1] = screen[xx_literal_load16(block + i + 1)];
        pairs[i + 2] = screen[xx_literal_load16(block + i + 2)];
        pairs[i + 3] = screen[xx_literal_load16(block + i + 3)];
    }
    pairs[XX_LITERAL_BLOCK] = screen[xx_literal_load16(block + XX_LITERAL_BLOCK)];
    pairs[XX_LITERAL_BLOCK + 1] = screen[xx_literal_load16(block + XX_LITERAL_BLOCK + 1)];
}
#endif

#define XX_LITERAL_HAS_BLOCK(at) \
    (end - (at) >= XX_LITERAL_BLOCK && size - (at) >= XX_LITERAL_BLOCK + XX_LITERAL_BLOCK_SLACK)

/* Large sets without a selective nibble screen: branch-free pair-screen
 * lookups of 64 starts (plus the two offset-2 pairs behind them), combined
 * 16 starts per step. The lookups run one block ahead of the vector combine,
 * so its 16-byte loads read entries whose 4-byte stores have retired (no
 * store-forwarding stalls). */
XX_LITERAL_SSE2
bool xx_literal_scan_blocks_sse2(const xx_literal_set *set, const uint8_t *data, size_t size,
                                 size_t begin, size_t end, xx_literal_match_fn match, void *context)
{
#ifdef XX_LITERAL_X86
    const uint32_t *screen = set->pair_screen;
    const __m128i zero = _mm_setzero_si128();
    uint32_t buffers[2][XX_LITERAL_BLOCK + 4];
    unsigned current = 0;
    size_t position = begin;
    bool ready = XX_LITERAL_HAS_BLOCK(position);
    if (ready) xx_literal_blocks_fill(screen, data + position, buffers[0]);
    while (ready) {
        const uint8_t *block = data + position;
        const uint32_t *pairs = buffers[current];
        size_t next = position + XX_LITERAL_BLOCK;
        uint64_t candidates;
        ready = XX_LITERAL_HAS_BLOCK(next);
        if (ready) xx_literal_blocks_fill(screen, data + next, buffers[current ^ 1U]);
        candidates = (uint64_t)xx_literal_blocks_zero16(pairs, zero);
        candidates |= (uint64_t)xx_literal_blocks_zero16(pairs + 16, zero) << 16;
        candidates |= (uint64_t)xx_literal_blocks_zero16(pairs + 32, zero) << 32;
        candidates |= (uint64_t)xx_literal_blocks_zero16(pairs + 48, zero) << 48;
        candidates = ~candidates;
        while (candidates != 0) {
            unsigned lane = xx_literal_first_bit64(candidates);
            uint8_t found = xx_literal_screen_lengths(set, block + lane, pairs[lane] & (pairs[lane + 2] >> 16));
            candidates &= candidates - 1;
            if (found != 0 && !xx_literal_verify_large_fast(set, data, position + lane, found, match, context)) return false;
        }
        position = next;
        current ^= 1U;
    }
    return xx_literal_scan_prepared(set, data, size, position, end, match, context);
#else
    return xx_literal_scan_prepared(set, data, size, begin, end, match, context);
#endif
}

#ifdef XX_LITERAL_X86
/* Gram-screen entries of a block: the pairs at its even offsets 0..64 (x86
 * is little-endian, so the 16-bit fields of an 8-byte load are the grams). */
static XX_LITERAL_INLINE void xx_literal_grams_fill(const uint32_t *screen, const uint8_t *block, uint32_t *grams)
{
    unsigned i;
    for (i = 0; i < XX_LITERAL_GRAMS - 1; i += 4) {
        uint64_t word = xx_literal_load64(block + 2 * i);
        grams[i] = screen[(uint16_t)word];
        grams[i + 1] = screen[(uint16_t)(word >> 16)];
        grams[i + 2] = screen[(uint16_t)(word >> 32)];
        grams[i + 3] = screen[(uint16_t)(word >> 48)];
    }
    grams[XX_LITERAL_GRAMS - 1] = screen[xx_literal_load16(block + XX_LITERAL_BLOCK)];
}

/* Zero-mask of the 16 starts of grams[0..8]: c_j = grams[j] & (grams[j + 1] >> 8)
 * masked to its even (bits 0-7) and odd (bits 16-23) roles is, as 16-bit
 * lanes, the even and the odd start in position order. */
XX_LITERAL_SSE2
static XX_LITERAL_INLINE uint32_t xx_literal_grams_zero16(const uint32_t *grams, __m128i roles, __m128i zero)
{
    __m128i c0 = _mm_and_si128(_mm_loadu_si128((const __m128i *)(const void *)grams),
                               _mm_srli_epi32(_mm_loadu_si128((const __m128i *)(const void *)(grams + 1)), 8));
    __m128i c1 = _mm_and_si128(_mm_loadu_si128((const __m128i *)(const void *)(grams + 4)),
                               _mm_srli_epi32(_mm_loadu_si128((const __m128i *)(const void *)(grams + 5)), 8));
    __m128i bytes = _mm_packus_epi16(_mm_and_si128(c0, roles), _mm_and_si128(c1, roles));
    return (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(bytes, zero));
}
#endif

/* Large sets with a gram screen: one lookup per two starts (see
 * xx_literal_prepare_grams), lookups one block ahead of the combine as in
 * xx_literal_scan_blocks_sse2; a passing start takes its lengths from its
 * role bits (and the pair length table) and is confirmed exactly. */
XX_LITERAL_SSE2
bool xx_literal_scan_grams_sse2(const xx_literal_set *set, const uint8_t *data, size_t size,
                                size_t begin, size_t end, xx_literal_match_fn match, void *context)
{
#ifdef XX_LITERAL_X86
    const uint32_t *screen = set->gram_screen;
    const __m128i zero = _mm_setzero_si128(), roles = _mm_set1_epi32(0x00ff00ff);
    uint32_t buffers[2][XX_LITERAL_GRAMS + 7];
    unsigned current = 0;
    size_t position = begin;
    bool ready = XX_LITERAL_HAS_BLOCK(position);
    if (ready) xx_literal_grams_fill(screen, data + position, buffers[0]);
    while (ready) {
        const uint8_t *block = data + position;
        const uint32_t *grams = buffers[current];
        size_t next = position + XX_LITERAL_BLOCK;
        uint64_t candidates, confirm = 0;
        uint8_t lane_lengths[XX_LITERAL_BLOCK];
        ready = XX_LITERAL_HAS_BLOCK(next);
        if (ready) xx_literal_grams_fill(screen, data + next, buffers[current ^ 1U]);
        candidates = (uint64_t)xx_literal_grams_zero16(grams, roles, zero);
        candidates |= (uint64_t)xx_literal_grams_zero16(grams + 8, roles, zero) << 16;
        candidates |= (uint64_t)xx_literal_grams_zero16(grams + 16, roles, zero) << 32;
        candidates |= (uint64_t)xx_literal_grams_zero16(grams + 24, roles, zero) << 48;
        candidates = ~candidates;
        /* Lengths of every candidate first (independent loads, no branch on
         * them), then the starts with lengths confirmed in increasing order. */
        while (candidates != 0) {
            unsigned lane = xx_literal_first_bit64(candidates), parity = lane & 1U;
            uint32_t passing = grams[lane >> 1] & (grams[(lane >> 1) + 1] >> 8);
            uint8_t lengths = set->gram_lengths[parity][(uint8_t)(passing >> (16U * parity))];
            candidates &= candidates - 1;
            lengths = xx_literal_screen_select(set, block + lane,
                                               (uint8_t)(set->pair_lengths[xx_literal_load16(block + lane)] & lengths));
            lane_lengths[lane] = lengths;
            confirm |= (uint64_t)(lengths != 0) << lane;
        }
        while (confirm != 0) {
            unsigned lane = xx_literal_first_bit64(confirm);
            confirm &= confirm - 1;
            if (!xx_literal_verify_large_fast(set, data, position + lane, lane_lengths[lane], match, context)) return false;
        }
        position = next;
        current ^= 1U;
    }
    return xx_literal_scan_prepared(set, data, size, position, end, match, context);
#else
    return xx_literal_scan_prepared(set, data, size, begin, end, match, context);
#endif
}
