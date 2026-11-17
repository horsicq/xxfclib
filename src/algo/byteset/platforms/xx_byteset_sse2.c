/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_byteset_sse2.c
 * @brief SSE2 byte-set search: 16 bytes per test, 64 per loop step. The
 *        compare and range strategies use SSE2 only; the table strategies
 *        (lookup, shufti, truffle) need SSSE3 pshufb and run only when the
 *        running CPU has it (xx_byteset_sse2_pshufb, checked per call),
 *        otherwise they take the scalar loop.
 */

#include "xx_byteset_platform.h"

#ifdef XX_BYTESET_X86

#define XX_BYTESET_SSE2_LOADU(p) _mm_loadu_si128((const __m128i *)(const void *)(p))
#define XX_BYTESET_SSE2_LOADA(p) _mm_load_si128((const __m128i *)(const void *)(p))

/* Compare and range strategies (SSE2). */
static XX_BYTESET_INLINE XX_BYTESET_TARGET_SSE2 __m128i xx_byteset_sse2_compare(int strategy, __m128i v, __m128i k0,
                                                                               __m128i k1, __m128i k2)
{
    switch (strategy) {
    case XX_BYTESET_STRATEGY_EQ1:
    case XX_BYTESET_STRATEGY_NE1:
        return _mm_cmpeq_epi8(v, k0);
    case XX_BYTESET_STRATEGY_EQ2:
    case XX_BYTESET_STRATEGY_NE2:
        return _mm_or_si128(_mm_cmpeq_epi8(v, k0), _mm_cmpeq_epi8(v, k1));
    case XX_BYTESET_STRATEGY_EQ3:
    case XX_BYTESET_STRATEGY_NE3:
        return _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, k0), _mm_cmpeq_epi8(v, k1)), _mm_cmpeq_epi8(v, k2));
    default: { /* RANGE, NOT_RANGE */
        __m128i offset = _mm_sub_epi8(v, k0); /* b - first, modulo 256 */
        return _mm_cmpeq_epi8(_mm_min_epu8(offset, k1), offset);
    }
    }
}

/* Table strategies (SSSE3 pshufb). */
static XX_BYTESET_INLINE XX_BYTESET_TARGET_SSSE3 __m128i xx_byteset_ssse3_shuffle(int strategy, __m128i v, __m128i k0,
                                                                                 __m128i k1, __m128i k2, __m128i k3,
                                                                                 __m128i k4)
{
    switch (strategy) {
    case XX_BYTESET_STRATEGY_LOOKUP: /* the only member with this low nibble, compared */
        return _mm_cmpeq_epi8(_mm_shuffle_epi8(k0, _mm_and_si128(v, k2)), v);
    case XX_BYTESET_STRATEGY_SHUFTI:
    case XX_BYTESET_STRATEGY_SHUFTI_FILTER: /* the buckets that hold the byte */
        return _mm_and_si128(_mm_shuffle_epi8(k0, _mm_and_si128(v, k2)),
                             _mm_shuffle_epi8(k1, _mm_and_si128(_mm_srli_epi16(v, 4), k2)));
    case XX_BYTESET_STRATEGY_TRUFFLE_ASCII: /* a shuffle index with bit 7 set yields zero */
        return _mm_and_si128(_mm_shuffle_epi8(k0, v), _mm_shuffle_epi8(k4, _mm_and_si128(_mm_srli_epi16(v, 4), k2)));
    default: { /* TRUFFLE */
        __m128i row = _mm_or_si128(_mm_shuffle_epi8(k0, v), _mm_shuffle_epi8(k1, _mm_xor_si128(v, k3)));
        __m128i bit = _mm_shuffle_epi8(k4, _mm_and_si128(_mm_srli_epi16(v, 4), k2));
        return _mm_and_si128(row, bit); /* the byte's bit of its row */
    }
    }
}

/* Member lanes of one test result, as bits. */
static XX_BYTESET_INLINE XX_BYTESET_TARGET_SSE2 uint32_t xx_byteset_sse2_bits(int strategy, __m128i result)
{
    if (XX_BYTESET_NONZERO(strategy))
        return (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(result, _mm_setzero_si128())) ^ 0xffffU;
    if (XX_BYTESET_NEGATIVE(strategy)) return (uint32_t)_mm_movemask_epi8(result) ^ 0xffffU;
    return (uint32_t)_mm_movemask_epi8(result);
}

/* True when one of four test results has a member lane. */
static XX_BYTESET_INLINE XX_BYTESET_TARGET_SSE2 bool xx_byteset_sse2_any(int strategy, __m128i a, __m128i b, __m128i c,
                                                                        __m128i d)
{
    if (XX_BYTESET_NEGATIVE(strategy))
        return _mm_movemask_epi8(_mm_and_si128(_mm_and_si128(a, b), _mm_and_si128(c, d))) != 0xffff;
    a = _mm_or_si128(_mm_or_si128(a, b), _mm_or_si128(c, d));
    if (XX_BYTESET_NONZERO(strategy)) return _mm_movemask_epi8(_mm_cmpeq_epi8(a, _mm_setzero_si128())) != 0xffff;
    return _mm_movemask_epi8(a) != 0;
}

/* Return the hit at base + the lowest candidate bit; SHUFTI_FILTER returns
 * the first confirmed candidate and falls through when none is a member. */
#define XX_BYTESET_SSE2_HIT(base, candidates)                                                    \
    do {                                                                                         \
        if (strategy != XX_BYTESET_STRATEGY_SHUFTI_FILTER) return (base) + xx_byteset_first_bit(candidates); \
        {                                                                                        \
            size_t hit_ = xx_byteset_confirm(set, data, (base), (candidates));                   \
            if (hit_ != SIZE_MAX) return hit_;                                                   \
        }                                                                                        \
    } while (0)

/* The search, shared by the SSE2 and SSSE3 functions; TEST(v) classifies one
 * loaded vector. Unaligned first test, aligned 64-byte steps from the next
 * 16-byte boundary, then the last 16 bytes of the buffer for the remainder. */
#define XX_BYTESET_SSE2_SCAN(TEST)                                                                 \
    uint32_t bits;                                                                                 \
    size_t position;                                                                               \
    unsigned step;                                                                                 \
    if (size - from >= 16U) {                                                                      \
        bits = xx_byteset_sse2_bits(strategy, TEST(XX_BYTESET_SSE2_LOADU(data + from)));           \
        if (bits) XX_BYTESET_SSE2_HIT(from, bits);                                                 \
        position = from + 16U - (size_t)((uintptr_t)(data + from) & 15U);                         \
        if (size >= 64U) {                                                                         \
            const size_t last = size - 64U; /* last start of a whole 64-byte step */                \
            for (;;) {                                                                             \
                /* Combined test of 64 bytes; the 16-byte tests run only on a     \
                 * hit, so the loop keeps no per-vector results alive. */          \
                while (position <= last &&                                                         \
                       !xx_byteset_sse2_any(strategy, TEST(XX_BYTESET_SSE2_LOADA(data + position)),  \
                                            TEST(XX_BYTESET_SSE2_LOADA(data + position + 16U)),     \
                                            TEST(XX_BYTESET_SSE2_LOADA(data + position + 32U)),     \
                                            TEST(XX_BYTESET_SSE2_LOADA(data + position + 48U))))    \
                    position += 64U;                                                               \
                if (position > last) break;                                                        \
                for (step = 0; step < 4U; ++step, position += 16U) {                               \
                    bits = xx_byteset_sse2_bits(strategy, TEST(XX_BYTESET_SSE2_LOADA(data + position))); \
                    if (bits) XX_BYTESET_SSE2_HIT(position, bits);                                 \
                }                                                                                  \
            }                                                                                      \
        }                                                                                          \
        while (size - position >= 16U) {                                                           \
            bits = xx_byteset_sse2_bits(strategy, TEST(XX_BYTESET_SSE2_LOADA(data + position)));   \
            if (bits) XX_BYTESET_SSE2_HIT(position, bits);                                         \
            position += 16U;                                                                       \
        }                                                                                          \
        if (position == size) return size;                                                         \
        from = position;                                                                           \
    } else if (size < 16U) {                                                                       \
        return xx_byteset_find_scalar(set, data, size, from);                                      \
    }                                                                                              \
    /* 1..15 bytes remain: test the last 16 bytes of the buffer and drop the                       \
     * lanes before from (inside the buffer, never past its end). */                               \
    bits = xx_byteset_sse2_bits(strategy, TEST(XX_BYTESET_SSE2_LOADU(data + size - 16U))) >>       \
           (from - (size - 16U));                                                                  \
    if (bits) XX_BYTESET_SSE2_HIT(from, bits);                                                     \
    return size;

#define XX_BYTESET_SSE2_COMPARE(v) xx_byteset_sse2_compare(strategy, v, k0, k1, k2)
#define XX_BYTESET_SSSE3_SHUFFLE(v) xx_byteset_ssse3_shuffle(strategy, v, k0, k1, k2, k3, k4)

static XX_BYTESET_INLINE XX_BYTESET_TARGET_SSE2 size_t xx_byteset_sse2_scan(int strategy, const xx_byteset *set,
                                                                           const uint8_t *data, size_t size,
                                                                           size_t from)
{
    const __m128i k0 = _mm_set1_epi8((char)set->values[0]);
    const __m128i k1 = _mm_set1_epi8((char)set->values[1]);
    const __m128i k2 = _mm_set1_epi8((char)set->values[2]);
    XX_BYTESET_SSE2_SCAN(XX_BYTESET_SSE2_COMPARE)
}

static XX_BYTESET_INLINE XX_BYTESET_TARGET_SSSE3 size_t xx_byteset_ssse3_scan(int strategy, const xx_byteset *set,
                                                                             const uint8_t *data, size_t size,
                                                                             size_t from)
{
    const __m128i k0 = _mm_loadu_si128((const __m128i *)(const void *)set->table_low);
    const __m128i k1 = _mm_loadu_si128((const __m128i *)(const void *)set->table_high);
    const __m128i k2 = _mm_set1_epi8(0x0f);
    const __m128i k3 = _mm_set1_epi8(-128);
    const __m128i k4 = _mm_setr_epi8(1, 2, 4, 8, 16, 32, 64, -128, 1, 2, 4, 8, 16, 32, 64, -128);
    XX_BYTESET_SSE2_SCAN(XX_BYTESET_SSSE3_SHUFFLE)
}

/* One function per strategy, so each saves only the vector registers it
 * uses (xmm6..15 are callee-saved on Win64); the dispatcher tail-calls. */
#define XX_BYTESET_SSE2_DEFINE(name, scan, target, s)                                                  \
    static XX_BYTESET_NOINLINE target size_t name(const xx_byteset *set, const uint8_t *data, size_t size, \
                                                  size_t from)                                         \
    {                                                                                                  \
        return scan(s, set, data, size, from);                                                         \
    }

XX_BYTESET_SSE2_DEFINE(xx_byteset_sse2_eq1, xx_byteset_sse2_scan, XX_BYTESET_TARGET_SSE2, XX_BYTESET_STRATEGY_EQ1)
XX_BYTESET_SSE2_DEFINE(xx_byteset_sse2_eq2, xx_byteset_sse2_scan, XX_BYTESET_TARGET_SSE2, XX_BYTESET_STRATEGY_EQ2)
XX_BYTESET_SSE2_DEFINE(xx_byteset_sse2_eq3, xx_byteset_sse2_scan, XX_BYTESET_TARGET_SSE2, XX_BYTESET_STRATEGY_EQ3)
XX_BYTESET_SSE2_DEFINE(xx_byteset_sse2_ne1, xx_byteset_sse2_scan, XX_BYTESET_TARGET_SSE2, XX_BYTESET_STRATEGY_NE1)
XX_BYTESET_SSE2_DEFINE(xx_byteset_sse2_ne2, xx_byteset_sse2_scan, XX_BYTESET_TARGET_SSE2, XX_BYTESET_STRATEGY_NE2)
XX_BYTESET_SSE2_DEFINE(xx_byteset_sse2_ne3, xx_byteset_sse2_scan, XX_BYTESET_TARGET_SSE2, XX_BYTESET_STRATEGY_NE3)
XX_BYTESET_SSE2_DEFINE(xx_byteset_sse2_range, xx_byteset_sse2_scan, XX_BYTESET_TARGET_SSE2, XX_BYTESET_STRATEGY_RANGE)
XX_BYTESET_SSE2_DEFINE(xx_byteset_sse2_not_range, xx_byteset_sse2_scan, XX_BYTESET_TARGET_SSE2,
                       XX_BYTESET_STRATEGY_NOT_RANGE)
XX_BYTESET_SSE2_DEFINE(xx_byteset_ssse3_lookup, xx_byteset_ssse3_scan, XX_BYTESET_TARGET_SSSE3,
                       XX_BYTESET_STRATEGY_LOOKUP)
XX_BYTESET_SSE2_DEFINE(xx_byteset_ssse3_shufti, xx_byteset_ssse3_scan, XX_BYTESET_TARGET_SSSE3,
                       XX_BYTESET_STRATEGY_SHUFTI)
XX_BYTESET_SSE2_DEFINE(xx_byteset_ssse3_shufti_filter, xx_byteset_ssse3_scan, XX_BYTESET_TARGET_SSSE3,
                       XX_BYTESET_STRATEGY_SHUFTI_FILTER)
XX_BYTESET_SSE2_DEFINE(xx_byteset_ssse3_truffle_ascii, xx_byteset_ssse3_scan, XX_BYTESET_TARGET_SSSE3,
                       XX_BYTESET_STRATEGY_TRUFFLE_ASCII)
XX_BYTESET_SSE2_DEFINE(xx_byteset_ssse3_truffle, xx_byteset_ssse3_scan, XX_BYTESET_TARGET_SSSE3,
                       XX_BYTESET_STRATEGY_TRUFFLE)

size_t xx_byteset_find_sse2(const xx_byteset *set, const uint8_t *data, size_t size, size_t from)
{
    switch (set->strategy) {
    case XX_BYTESET_STRATEGY_EQ1: return xx_byteset_sse2_eq1(set, data, size, from);
    case XX_BYTESET_STRATEGY_EQ2: return xx_byteset_sse2_eq2(set, data, size, from);
    case XX_BYTESET_STRATEGY_EQ3: return xx_byteset_sse2_eq3(set, data, size, from);
    case XX_BYTESET_STRATEGY_NE1: return xx_byteset_sse2_ne1(set, data, size, from);
    case XX_BYTESET_STRATEGY_NE2: return xx_byteset_sse2_ne2(set, data, size, from);
    case XX_BYTESET_STRATEGY_NE3: return xx_byteset_sse2_ne3(set, data, size, from);
    case XX_BYTESET_STRATEGY_RANGE: return xx_byteset_sse2_range(set, data, size, from);
    case XX_BYTESET_STRATEGY_NOT_RANGE: return xx_byteset_sse2_not_range(set, data, size, from);
    /* The shuffle strategies need SSSE3 on this CPU; without it they take
     * the scalar loop. */
    case XX_BYTESET_STRATEGY_LOOKUP:
        if (xx_byteset_sse2_pshufb()) return xx_byteset_ssse3_lookup(set, data, size, from);
        break;
    case XX_BYTESET_STRATEGY_SHUFTI:
        if (xx_byteset_sse2_pshufb()) return xx_byteset_ssse3_shufti(set, data, size, from);
        break;
    case XX_BYTESET_STRATEGY_SHUFTI_FILTER:
        if (xx_byteset_sse2_pshufb()) return xx_byteset_ssse3_shufti_filter(set, data, size, from);
        break;
    case XX_BYTESET_STRATEGY_TRUFFLE_ASCII:
        if (xx_byteset_sse2_pshufb()) return xx_byteset_ssse3_truffle_ascii(set, data, size, from);
        break;
    case XX_BYTESET_STRATEGY_TRUFFLE:
        if (xx_byteset_sse2_pshufb()) return xx_byteset_ssse3_truffle(set, data, size, from);
        break;
    default: break;
    }
    return xx_byteset_find_scalar(set, data, size, from);
}

#endif /* XX_BYTESET_X86 */
