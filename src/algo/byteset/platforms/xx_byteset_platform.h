/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_BYTESET_PLATFORM_H
#define XX_BYTESET_PLATFORM_H

#include "xxfclib/algo/byteset/xx_byteset.h"
#include "xxfclib/global/xx_global.h"

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)) && !defined(_M_ARM64EC)
#define XX_BYTESET_X86 1
#include <immintrin.h>
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_BYTESET_X86)
#define XX_BYTESET_TARGET_SSE2 __attribute__((target("sse2")))
#define XX_BYTESET_TARGET_SSSE3 __attribute__((target("ssse3")))
#define XX_BYTESET_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_BYTESET_TARGET_SSE2
#define XX_BYTESET_TARGET_SSSE3
#define XX_BYTESET_TARGET_AVX2
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#define XX_BYTESET_INLINE __forceinline
#define XX_BYTESET_NOINLINE __declspec(noinline)
static __inline unsigned xx_byteset_first_bit(uint32_t bits)
{
    unsigned long index;
    _BitScanForward(&index, bits);
    return (unsigned)index;
}
#elif defined(__GNUC__) || defined(__clang__)
#define XX_BYTESET_INLINE inline __attribute__((always_inline))
#define XX_BYTESET_NOINLINE __attribute__((noinline))
static inline unsigned xx_byteset_first_bit(uint32_t bits)
{
    return (unsigned)__builtin_ctz(bits);
}
#else
#define XX_BYTESET_INLINE inline
#define XX_BYTESET_NOINLINE
static inline unsigned xx_byteset_first_bit(uint32_t bits)
{
    unsigned index = 0;
    while (!(bits & 1U)) { bits >>= 1; ++index; }
    return index;
}
#endif

/* Search strategies (xx_byteset.strategy), with the vector operations each
 * spends per tested vector after the load. Every strategy except
 * SHUFTI_FILTER classifies each byte exactly; SHUFTI_FILTER accepts a
 * superset of the members and confirms each candidate against bits. */
#define XX_BYTESET_STRATEGY_UNPREPARED 0     /* scalar bitmap loop in every backend */
#define XX_BYTESET_STRATEGY_EMPTY 1          /* no member: return size */
#define XX_BYTESET_STRATEGY_FULL 2           /* every value: return from */
#define XX_BYTESET_STRATEGY_EQ1 3            /* b == values[0]: 1 op */
#define XX_BYTESET_STRATEGY_EQ2 4            /* b in values[0..1]: 3 ops */
#define XX_BYTESET_STRATEGY_EQ3 5            /* b in values[0..2]: 5 ops */
#define XX_BYTESET_STRATEGY_NE1 6            /* b != values[0] */
#define XX_BYTESET_STRATEGY_NE2 7            /* b not in values[0..1] */
#define XX_BYTESET_STRATEGY_NE3 8            /* b not in values[0..2] */
#define XX_BYTESET_STRATEGY_RANGE 9          /* (uint8_t)(b - values[0]) <= values[1]: 3 ops */
#define XX_BYTESET_STRATEGY_NOT_RANGE 10     /* (uint8_t)(b - values[0]) > values[1]: 3 ops */
#define XX_BYTESET_STRATEGY_LOOKUP 11        /* table_low[b & 15] == b (members differ in b & 15): 3 ops */
#define XX_BYTESET_STRATEGY_SHUFTI 12        /* table_low[b & 15] & table_high[b >> 4] != 0, exact: 6 ops */
#define XX_BYTESET_STRATEGY_SHUFTI_FILTER 13 /* same test, a superset: confirm against bits */
#define XX_BYTESET_STRATEGY_TRUFFLE_ASCII 14 /* b < 0x80 && bit (b >> 4) of table_low[b & 15]: 5 ops */
#define XX_BYTESET_STRATEGY_TRUFFLE 15       /* bit (b >> 4) & 7 of (b < 0x80 ? low : high)[b & 15]: 8 ops */
#define XX_BYTESET_STRATEGY_COUNT 16

/* The result vector of one test holds 0xff in member lanes and 0 elsewhere
 * (positive), 0xff in non-member lanes and 0 elsewhere (negative), or any
 * non-zero byte in member lanes and 0 elsewhere (nonzero form: the vectors
 * of one step are OR'ed before their single compare with zero). Every hit
 * test ends in movemask; vptest is avoided, it is slow on Zen 1. */
#define XX_BYTESET_NEGATIVE(s)                                                                     \
    ((s) == XX_BYTESET_STRATEGY_NE1 || (s) == XX_BYTESET_STRATEGY_NE2 || (s) == XX_BYTESET_STRATEGY_NE3 || \
     (s) == XX_BYTESET_STRATEGY_NOT_RANGE)
#define XX_BYTESET_NONZERO(s)                                                                    \
    ((s) == XX_BYTESET_STRATEGY_SHUFTI || (s) == XX_BYTESET_STRATEGY_SHUFTI_FILTER ||                \
     (s) == XX_BYTESET_STRATEGY_TRUFFLE_ASCII || (s) == XX_BYTESET_STRATEGY_TRUFFLE)
/* Strategies that index tables with pshufb (SSSE3 in the SSE2 backend). */
#define XX_BYTESET_SHUFFLES(s) ((s) == XX_BYTESET_STRATEGY_LOOKUP || XX_BYTESET_NONZERO(s))

static XX_BYTESET_INLINE bool xx_byteset_member(const xx_byteset *set, uint8_t value)
{
    return ((set->bits[value >> 6] >> (value & 63U)) & 1U) != 0;
}

/* Lowest i in base + (the set bits of candidates) whose byte is a member,
 * or SIZE_MAX; confirms the candidates of SHUFTI_FILTER. */
static XX_BYTESET_INLINE size_t xx_byteset_confirm(const xx_byteset *set, const uint8_t *data, size_t base,
                                                   uint32_t candidates)
{
    do {
        size_t i = base + xx_byteset_first_bit(candidates);
        if (xx_byteset_member(set, data[i])) return i;
        candidates &= candidates - 1U;
    } while (candidates);
    return SIZE_MAX;
}

/* Backends receive from < size, data != NULL and a strategy other than
 * EMPTY and FULL. */
size_t xx_byteset_find_scalar(const xx_byteset *set, const uint8_t *data, size_t size, size_t from);

#ifdef XX_BYTESET_X86
size_t xx_byteset_find_sse2(const xx_byteset *set, const uint8_t *data, size_t size, size_t from);
size_t xx_byteset_find_avx2(const xx_byteset *set, const uint8_t *data, size_t size, size_t from);

/* Whether the SSE2 backend may run its SSSE3 (pshufb) kernels, asked of the
 * running CPU on every call and never recorded in a set: a set prepared on
 * one CPU and copied to another cannot reach pshufb where it does not exist.
 * xxfclib does not report SSSE3 separately; every SSE4.2 processor implements
 * it (xx_regex_search.c relies on the same). Turning SSE4.2 off, or a CPU
 * without it, sends the shuffle strategies to the scalar loop. AVX2 implies
 * SSSE3, so the AVX2 backend does not ask. */
static XX_BYTESET_INLINE bool xx_byteset_sse2_pshufb(void)
{
    return xx_is_sse42_enabled();
}
#endif

/* Prepare the set with one given strategy instead of the chosen one, for
 * tests and benchmarks. Returns false, leaving the set unprepared, when that
 * strategy cannot search this set. SHUFTI_FILTER and TRUFFLE take any set. */
bool xx_byteset_prepare_strategy(xx_byteset *set, int strategy);

#endif /* XX_BYTESET_PLATFORM_H */
