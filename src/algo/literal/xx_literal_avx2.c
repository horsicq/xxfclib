/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_literal_internal.h"

#ifdef XX_LITERAL_X86
/* Nibble screen of 32 starts over filter_bytes leading bytes (a constant
 * after inlining): bucket bits per start, zero for rejected starts. */
#define XX_LITERAL_AVX2_STEP(j)                                                                         \
    do {                                                                                                \
        __m256i bytes = _mm256_loadu_si256((const __m256i *)(const void *)(data + position + (j)));    \
        __m256i lo = _mm256_and_si256(bytes, nibbles);                                                  \
        __m256i hi = _mm256_and_si256(_mm256_srli_epi16(bytes, 4), nibbles);                            \
        candidates = _mm256_and_si256(candidates, _mm256_and_si256(_mm256_shuffle_epi8(low[j], lo),    \
                                                                   _mm256_shuffle_epi8(high[j], hi))); \
    } while (0)

XX_LITERAL_AVX2
static XX_LITERAL_INLINE bool xx_literal_avx2_loop(const xx_literal_set *set, const uint8_t *data, size_t size,
                                                   size_t *cursor, size_t end, unsigned filter_bytes,
                                                   xx_literal_match_fn match, void *context)
{
    __m256i low[4], high[4];
    const __m256i nibbles = _mm256_set1_epi8(15);
    const __m256i zero = _mm256_setzero_si256();
    size_t position = *cursor;
    unsigned j;
    for (j = 0; j < filter_bytes; ++j) {
        low[j] = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)(const void *)set->low[j]));
        high[j] = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)(const void *)set->high[j]));
    }
    /* 32 starts plus 8 readable bytes behind the last one for confirmation. */
    while (end - position >= 32 && size - position >= 32U + XX_LITERAL_BLOCK_SLACK) {
        __m256i candidates = _mm256_set1_epi8((char)255);
        uint32_t bits;
        XX_LITERAL_AVX2_STEP(0);
        if (filter_bytes > 1) XX_LITERAL_AVX2_STEP(1);
        if (filter_bytes > 2) XX_LITERAL_AVX2_STEP(2);
        if (filter_bytes > 3) XX_LITERAL_AVX2_STEP(3);
        bits = (uint32_t)~_mm256_movemask_epi8(_mm256_cmpeq_epi8(candidates, zero));
        if (bits != 0) {
            uint8_t buckets[32];
            _mm256_storeu_si256((__m256i *)(void *)buckets, candidates);
            do {
                unsigned lane = xx_literal_first_bit(bits);
                size_t at = position + lane;
                bits &= bits - 1;
                if (set->small) {
                    if (!xx_literal_verify_small_fast(set, data, at, buckets[lane], match, context)) {
                        _mm256_zeroupper();
                        return false;
                    }
                } else {
                    uint8_t lengths = xx_literal_screen(set, data + at, set->pair_lengths[xx_literal_load16(data + at)]);
                    if (lengths != 0 && !xx_literal_verify_large_fast(set, data, at, lengths, match, context)) {
                        _mm256_zeroupper();
                        return false;
                    }
                }
            } while (bits != 0);
        }
        position += 32;
    }
    _mm256_zeroupper();
    *cursor = position;
    return true;
}
#endif

XX_LITERAL_AVX2
bool xx_literal_scan_avx2(const xx_literal_set *set, const uint8_t *data, size_t size,
                          size_t begin, size_t end, xx_literal_match_fn match, void *context)
{
#ifdef XX_LITERAL_X86
    size_t position = begin;
    bool ok;
    switch (set->filter_bytes) {
    case 1: ok = xx_literal_avx2_loop(set, data, size, &position, end, 1, match, context); break;
    case 2: ok = xx_literal_avx2_loop(set, data, size, &position, end, 2, match, context); break;
    case 3: ok = xx_literal_avx2_loop(set, data, size, &position, end, 3, match, context); break;
    default: ok = xx_literal_avx2_loop(set, data, size, &position, end, 4, match, context); break;
    }
    if (!ok) return false;
    return xx_literal_scan_prepared(set, data, size, position, end, match, context);
#else
    return xx_literal_scan_prepared(set, data, size, begin, end, match, context);
#endif
}
