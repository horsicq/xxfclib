/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_byteset_avx2.c
 * @brief AVX2 byte-set search: 32 bytes per test, 64 per loop step.
 */

#include "xx_byteset_platform.h"

#ifdef XX_BYTESET_X86

#define XX_BYTESET_AVX2_LOAD(p) _mm256_loadu_si256((const __m256i *)(const void *)(p))

/* Test 32 bytes; the strategy is a constant after inlining. k0..k4 hold the
 * broadcast values or tables the strategy uses. */
static XX_BYTESET_INLINE XX_BYTESET_TARGET_AVX2 __m256i xx_byteset_avx2_test(int strategy, __m256i v, __m256i k0,
                                                                            __m256i k1, __m256i k2, __m256i k3,
                                                                            __m256i k4)
{
    switch (strategy) {
    case XX_BYTESET_STRATEGY_EQ1:
    case XX_BYTESET_STRATEGY_NE1:
        return _mm256_cmpeq_epi8(v, k0);
    case XX_BYTESET_STRATEGY_EQ2:
    case XX_BYTESET_STRATEGY_NE2:
        return _mm256_or_si256(_mm256_cmpeq_epi8(v, k0), _mm256_cmpeq_epi8(v, k1));
    case XX_BYTESET_STRATEGY_EQ3:
    case XX_BYTESET_STRATEGY_NE3:
        return _mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi8(v, k0), _mm256_cmpeq_epi8(v, k1)),
                               _mm256_cmpeq_epi8(v, k2));
    case XX_BYTESET_STRATEGY_RANGE:
    case XX_BYTESET_STRATEGY_NOT_RANGE: {
        __m256i offset = _mm256_sub_epi8(v, k0); /* b - first, modulo 256 */
        return _mm256_cmpeq_epi8(_mm256_min_epu8(offset, k1), offset);
    }
    case XX_BYTESET_STRATEGY_LOOKUP: /* the only member with this low nibble, compared */
        return _mm256_cmpeq_epi8(_mm256_shuffle_epi8(k0, _mm256_and_si256(v, k2)), v);
    case XX_BYTESET_STRATEGY_SHUFTI:
    case XX_BYTESET_STRATEGY_SHUFTI_FILTER: /* the buckets that hold the byte */
        return _mm256_and_si256(_mm256_shuffle_epi8(k0, _mm256_and_si256(v, k2)),
                                _mm256_shuffle_epi8(k1, _mm256_and_si256(_mm256_srli_epi16(v, 4), k2)));
    case XX_BYTESET_STRATEGY_TRUFFLE_ASCII: /* a shuffle index with bit 7 set yields zero */
        return _mm256_and_si256(_mm256_shuffle_epi8(k0, v),
                                _mm256_shuffle_epi8(k4, _mm256_and_si256(_mm256_srli_epi16(v, 4), k2)));
    default: { /* TRUFFLE */
        __m256i row = _mm256_or_si256(_mm256_shuffle_epi8(k0, v), _mm256_shuffle_epi8(k1, _mm256_xor_si256(v, k3)));
        __m256i bit = _mm256_shuffle_epi8(k4, _mm256_and_si256(_mm256_srli_epi16(v, 4), k2));
        return _mm256_and_si256(row, bit); /* the byte's bit of its row */
    }
    }
}

/* Member lanes of one test result, as bits. */
static XX_BYTESET_INLINE XX_BYTESET_TARGET_AVX2 uint32_t xx_byteset_avx2_bits(int strategy, __m256i result)
{
    if (XX_BYTESET_NONZERO(strategy))
        return ~(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(result, _mm256_setzero_si256()));
    if (XX_BYTESET_NEGATIVE(strategy)) return ~(uint32_t)_mm256_movemask_epi8(result);
    return (uint32_t)_mm256_movemask_epi8(result);
}

/* True when one of two test results has a member lane. */
static XX_BYTESET_INLINE XX_BYTESET_TARGET_AVX2 bool xx_byteset_avx2_any(int strategy, __m256i a, __m256i b)
{
    if (XX_BYTESET_NONZERO(strategy))
        return (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_or_si256(a, b), _mm256_setzero_si256())) !=
               0xffffffffU;
    if (XX_BYTESET_NEGATIVE(strategy)) return (uint32_t)_mm256_movemask_epi8(_mm256_and_si256(a, b)) != 0xffffffffU;
    return _mm256_movemask_epi8(_mm256_or_si256(a, b)) != 0;
}

/* Return the hit at base + the lowest candidate bit; SHUFTI_FILTER returns
 * the first confirmed candidate and falls through when none is a member. */
#define XX_BYTESET_AVX2_HIT(base, candidates)                                                    \
    do {                                                                                         \
        if (strategy != XX_BYTESET_STRATEGY_SHUFTI_FILTER) return (base) + xx_byteset_first_bit(candidates); \
        {                                                                                        \
            size_t hit_ = xx_byteset_confirm(set, data, (base), (candidates));                   \
            if (hit_ != SIZE_MAX) return hit_;                                                   \
        }                                                                                        \
    } while (0)

#define XX_BYTESET_AVX2_TEST(p) xx_byteset_avx2_test(strategy, XX_BYTESET_AVX2_LOAD(p), k0, k1, k2, k3, k4)

static XX_BYTESET_INLINE XX_BYTESET_TARGET_AVX2 size_t xx_byteset_avx2_scan(int strategy, const xx_byteset *set,
                                                                           const uint8_t *data, size_t size,
                                                                           size_t from)
{
    __m256i k0, k1, k2, k3, k4;
    uint32_t bits;
    size_t position;
    if (XX_BYTESET_SHUFFLES(strategy)) {
        k0 = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)(const void *)set->table_low));
        k1 = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)(const void *)set->table_high));
        k2 = _mm256_set1_epi8(0x0f);
        k3 = _mm256_set1_epi8(-128);
        k4 = _mm256_setr_epi8(1, 2, 4, 8, 16, 32, 64, -128, 1, 2, 4, 8, 16, 32, 64, -128,
                              1, 2, 4, 8, 16, 32, 64, -128, 1, 2, 4, 8, 16, 32, 64, -128);
    } else {
        k0 = _mm256_set1_epi8((char)set->values[0]);
        k1 = _mm256_set1_epi8((char)set->values[1]);
        k2 = _mm256_set1_epi8((char)set->values[2]);
        k3 = k4 = _mm256_setzero_si256();
    }
    if (size - from >= 32U) {
        /* An unaligned first test keeps a call that hits soon cheap; then
         * aligned steps from the next 32-byte boundary (they overlap the
         * first test, which found nothing). */
        bits = xx_byteset_avx2_bits(strategy, XX_BYTESET_AVX2_TEST(data + from));
        if (bits) XX_BYTESET_AVX2_HIT(from, bits);
        position = from + 32U - (size_t)((uintptr_t)(data + from) & 31U);
        if (size >= 64U) {
            const size_t last = size - 64U; /* last start of a whole 64-byte step */
            while (position <= last) {
                __m256i a = XX_BYTESET_AVX2_TEST(data + position);
                __m256i b = XX_BYTESET_AVX2_TEST(data + position + 32U);
                if (xx_byteset_avx2_any(strategy, a, b)) {
                    bits = xx_byteset_avx2_bits(strategy, a);
                    if (bits) XX_BYTESET_AVX2_HIT(position, bits);
                    bits = xx_byteset_avx2_bits(strategy, b);
                    if (bits) XX_BYTESET_AVX2_HIT(position + 32U, bits);
                }
                position += 64U;
            }
        }
        if (size - position >= 32U) {
            bits = xx_byteset_avx2_bits(strategy, XX_BYTESET_AVX2_TEST(data + position));
            if (bits) XX_BYTESET_AVX2_HIT(position, bits);
            position += 32U;
        }
        if (position == size) return size;
        from = position;
    } else if (size < 32U) {
        return xx_byteset_find_scalar(set, data, size, from);
    }
    /* 1..31 bytes remain: test the last 32 bytes of the buffer and drop the
     * lanes before from (they are inside the buffer, never past its end). */
    bits = xx_byteset_avx2_bits(strategy, XX_BYTESET_AVX2_TEST(data + size - 32U)) >> (from - (size - 32U));
    if (bits) XX_BYTESET_AVX2_HIT(from, bits);
    return size;
}

/* One function per strategy, so each saves only the vector registers it
 * uses (xmm6..15 are callee-saved on Win64); the dispatcher tail-calls. */
#define XX_BYTESET_AVX2_DEFINE(name, s)                                                                \
    static XX_BYTESET_NOINLINE XX_BYTESET_TARGET_AVX2 size_t name(const xx_byteset *set, const uint8_t *data, \
                                                                  size_t size, size_t from)            \
    {                                                                                                  \
        return xx_byteset_avx2_scan(s, set, data, size, from);                                        \
    }

XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_eq1, XX_BYTESET_STRATEGY_EQ1)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_eq2, XX_BYTESET_STRATEGY_EQ2)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_eq3, XX_BYTESET_STRATEGY_EQ3)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_ne1, XX_BYTESET_STRATEGY_NE1)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_ne2, XX_BYTESET_STRATEGY_NE2)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_ne3, XX_BYTESET_STRATEGY_NE3)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_range, XX_BYTESET_STRATEGY_RANGE)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_not_range, XX_BYTESET_STRATEGY_NOT_RANGE)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_lookup, XX_BYTESET_STRATEGY_LOOKUP)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_shufti, XX_BYTESET_STRATEGY_SHUFTI)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_shufti_filter, XX_BYTESET_STRATEGY_SHUFTI_FILTER)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_truffle_ascii, XX_BYTESET_STRATEGY_TRUFFLE_ASCII)
XX_BYTESET_AVX2_DEFINE(xx_byteset_avx2_truffle, XX_BYTESET_STRATEGY_TRUFFLE)

size_t xx_byteset_find_avx2(const xx_byteset *set, const uint8_t *data, size_t size, size_t from)
{
    switch (set->strategy) {
    case XX_BYTESET_STRATEGY_EQ1: return xx_byteset_avx2_eq1(set, data, size, from);
    case XX_BYTESET_STRATEGY_EQ2: return xx_byteset_avx2_eq2(set, data, size, from);
    case XX_BYTESET_STRATEGY_EQ3: return xx_byteset_avx2_eq3(set, data, size, from);
    case XX_BYTESET_STRATEGY_NE1: return xx_byteset_avx2_ne1(set, data, size, from);
    case XX_BYTESET_STRATEGY_NE2: return xx_byteset_avx2_ne2(set, data, size, from);
    case XX_BYTESET_STRATEGY_NE3: return xx_byteset_avx2_ne3(set, data, size, from);
    case XX_BYTESET_STRATEGY_RANGE: return xx_byteset_avx2_range(set, data, size, from);
    case XX_BYTESET_STRATEGY_NOT_RANGE: return xx_byteset_avx2_not_range(set, data, size, from);
    case XX_BYTESET_STRATEGY_LOOKUP: return xx_byteset_avx2_lookup(set, data, size, from);
    case XX_BYTESET_STRATEGY_SHUFTI: return xx_byteset_avx2_shufti(set, data, size, from);
    case XX_BYTESET_STRATEGY_SHUFTI_FILTER: return xx_byteset_avx2_shufti_filter(set, data, size, from);
    case XX_BYTESET_STRATEGY_TRUFFLE_ASCII: return xx_byteset_avx2_truffle_ascii(set, data, size, from);
    case XX_BYTESET_STRATEGY_TRUFFLE: return xx_byteset_avx2_truffle(set, data, size, from);
    default: return xx_byteset_find_scalar(set, data, size, from);
    }
}

#endif /* XX_BYTESET_X86 */
