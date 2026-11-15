/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_regex_search.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"

#if !defined(_M_ARM64EC) && (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__))
#define XX_REGEX_X86 1
#include <emmintrin.h>
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

#if !defined(_M_ARM64EC) && (defined(_M_ARM64) || defined(__aarch64__))
#define XX_REGEX_NEON 1
#include <arm_neon.h>
#endif

#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_REGEX_X86)
#define XX_REGEX_SSE2 __attribute__((target("sse2")))
#define XX_REGEX_SSSE3 __attribute__((target("ssse3")))
#define XX_REGEX_AVX2 __attribute__((target("avx2")))
#else
#define XX_REGEX_SSE2
#define XX_REGEX_SSSE3
#define XX_REGEX_AVX2
#endif

static const unsigned char *find_pair_scalar(const unsigned char *text, size_t candidates,
                                            unsigned char first, size_t offset,
                                            unsigned char second)
{
    size_t at;
    for (at = 0; at < candidates; ++at) {
        if (text[at] == first && text[at + offset] == second) return text + at;
    }
    return NULL;
}

#ifdef XX_REGEX_X86
XX_REGEX_SSE2
static const unsigned char *find_pair_sse2(const unsigned char *text, size_t candidates,
                                          unsigned char first, size_t offset,
                                          unsigned char second)
{
    const __m128i a = _mm_set1_epi8((char)first);
    const __m128i b = _mm_set1_epi8((char)second);
    size_t at = 0;
    for (; candidates - at >= 16; at += 16) {
        const __m128i left = _mm_loadu_si128((const __m128i *)(const void *)(text + at));
        const __m128i right = _mm_loadu_si128((const __m128i *)(const void *)(text + at + offset));
        unsigned int mask = (unsigned int)_mm_movemask_epi8(_mm_and_si128(_mm_cmpeq_epi8(left, a), _mm_cmpeq_epi8(right, b)));
        if (mask) {
#if defined(_MSC_VER)
            unsigned long bit;
            _BitScanForward(&bit, mask);
            return text + at + bit;
#else
            return text + at + (unsigned int)__builtin_ctz(mask);
#endif
        }
    }
    return find_pair_scalar(text + at, candidates - at, first, offset, second);
}
#endif

#ifdef XX_REGEX_NEON
static const unsigned char *find_pair_neon(const unsigned char *text, size_t candidates,
                                          unsigned char first, size_t offset,
                                          unsigned char second)
{
    const uint8x16_t a = vdupq_n_u8(first);
    const uint8x16_t b = vdupq_n_u8(second);
    size_t at = 0;
    for (; candidates - at >= 16; at += 16) {
        const uint8x16_t equal = vandq_u8(vceqq_u8(vld1q_u8(text + at), a),
                                        vceqq_u8(vld1q_u8(text + at + offset), b));
        const uint64x2_t words = vreinterpretq_u64_u8(equal);
        if (vgetq_lane_u64(words, 0) || vgetq_lane_u64(words, 1)) {
            return find_pair_scalar(text + at, 16, first, offset, second);
        }
    }
    return find_pair_scalar(text + at, candidates - at, first, offset, second);
}
#endif

const unsigned char *xx_regex_find_pair(const unsigned char *text, size_t size,
                                       unsigned char first, size_t offset,
                                       unsigned char second)
{
    size_t candidates;
    const unsigned char *candidate;
    if (!text || offset >= size) return NULL;
    candidates = size - offset;
    /* Preserve the runtime's wide single-byte search for sparse inputs. Only
     * switch to the pair filter after an actual false candidate is found. */
    candidate = (const unsigned char *)xx_rt_memchr(text, first, candidates);
    if (!candidate || candidate[offset] == second) return candidate;
    candidates -= (size_t)(candidate - text) + 1;
    text = candidate + 1;
    if (candidates >= 32) {
#ifdef XX_REGEX_X86
        if (xx_is_sse2_enabled()) return find_pair_sse2(text, candidates, first, offset, second);
#endif
#ifdef XX_REGEX_NEON
        if (xx_is_neon_enabled()) return find_pair_neon(text, candidates, first, offset, second);
#endif
    }
    return find_pair_scalar(text, candidates, first, offset, second);
}

/* ------------------------------------------------------------ byte sets  */

void xx_regex_byteset_init(xx_regex_byteset *set, const unsigned char map[32], size_t offset)
{
    unsigned index, bit;
    xx_rt_memset(set, 0, sizeof(*set));
    set->offset = offset;
    xx_rt_memcpy(set->map, map, sizeof(set->map));
    for (index = 0; index < 32; ++index) {
        if (!map[index]) continue;
        for (bit = 0; bit < 8; ++bit) {
            unsigned value = index * 8 + bit;
            if (!(map[index] & (1u << bit))) continue;
            if (value < 0x80) set->low[value & 15] |= (unsigned char)(1u << (value >> 4));
            else {
                set->high[value & 15] |= (unsigned char)(1u << ((value >> 4) - 8));
                set->has_high = 1;
            }
            if (set->members < 3) set->bytes[set->members] = (unsigned char)value;
            ++set->members;
        }
    }
}

static int byteset_has(const xx_regex_byteset *set, unsigned char value)
{
    return (set->map[value >> 3] >> (value & 7)) & 1;
}

static const unsigned char *find_sets_scalar(const unsigned char *text, size_t candidates,
                                             const xx_regex_byteset *sets, size_t count)
{
    size_t at, j;
    for (at = 0; at < candidates; ++at) {
        for (j = 0; j < count; ++j)
            if (!byteset_has(&sets[j], text[at + sets[j].offset])) break;
        if (j == count) return text + at;
    }
    return NULL;
}

#ifdef XX_REGEX_X86
/* Small sets compare their members; larger ones look the low nibble up in
 * the table of their high-nibble half (pshufb yields zero for an index with
 * bit 7 set, which selects the half) and test the high nibble's bit. */
#define RE_SET_EQ1 1
#define RE_SET_EQ2 2
#define RE_SET_EQ3 3
#define RE_SET_LOW 4
#define RE_SET_FULL 5

static int byteset_kind(const xx_regex_byteset *set)
{
    if (set->members >= 1 && set->members <= 3) return (int)set->members;
    return set->has_high ? RE_SET_FULL : RE_SET_LOW;
}

static const unsigned char re_set_bits[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};

XX_REGEX_SSSE3
static const unsigned char *find_sets_ssse3(const unsigned char *text, size_t candidates,
                                            const xx_regex_byteset *sets, size_t count)
{
    __m128i low[XX_REGEX_MAX_SETS], high[XX_REGEX_MAX_SETS];
    __m128i b0[XX_REGEX_MAX_SETS], b1[XX_REGEX_MAX_SETS], b2[XX_REGEX_MAX_SETS];
    int kind[XX_REGEX_MAX_SETS];
    const __m128i bits = _mm_loadu_si128((const __m128i *)(const void *)re_set_bits);
    const __m128i keep_low = _mm_set1_epi8(-113 /* 0x8F */), flip = _mm_set1_epi8(-128 /* 0x80 */);
    const __m128i nibble = _mm_set1_epi8(0x0F);
    size_t at = 0, j;
    for (j = 0; j < count; ++j) {
        kind[j] = byteset_kind(&sets[j]);
        low[j] = _mm_loadu_si128((const __m128i *)(const void *)sets[j].low);
        high[j] = _mm_loadu_si128((const __m128i *)(const void *)sets[j].high);
        b0[j] = _mm_set1_epi8((char)sets[j].bytes[0]);
        b1[j] = _mm_set1_epi8((char)sets[j].bytes[1]);
        b2[j] = _mm_set1_epi8((char)sets[j].bytes[2]);
    }
    for (; candidates - at >= 16; at += 16) {
        __m128i all = _mm_set1_epi8(-1);
        unsigned int mask;
        for (j = 0; j < count; ++j) {
            const __m128i v = _mm_loadu_si128((const __m128i *)(const void *)(text + at + sets[j].offset));
            __m128i member;
            switch (kind[j]) {
                case RE_SET_EQ1: member = _mm_cmpeq_epi8(v, b0[j]); break;
                case RE_SET_EQ2: member = _mm_or_si128(_mm_cmpeq_epi8(v, b0[j]), _mm_cmpeq_epi8(v, b1[j])); break;
                case RE_SET_EQ3:
                    member = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, b0[j]), _mm_cmpeq_epi8(v, b1[j])),
                                          _mm_cmpeq_epi8(v, b2[j]));
                    break;
                default: {
                    __m128i row = _mm_shuffle_epi8(low[j], _mm_and_si128(v, keep_low));
                    const __m128i bit = _mm_shuffle_epi8(bits, _mm_and_si128(_mm_srli_epi16(v, 4), nibble));
                    if (kind[j] == RE_SET_FULL)
                        row = _mm_or_si128(row, _mm_shuffle_epi8(high[j], _mm_and_si128(_mm_xor_si128(v, flip), keep_low)));
                    member = _mm_cmpeq_epi8(_mm_and_si128(row, bit), bit);
                    break;
                }
            }
            all = _mm_and_si128(all, member);
            /* The first set is the most selective: most blocks end here. */
            if (!_mm_movemask_epi8(all)) break;
        }
        mask = (unsigned int)_mm_movemask_epi8(all);
        if (mask) {
#if defined(_MSC_VER)
            unsigned long bit;
            _BitScanForward(&bit, mask);
            return text + at + bit;
#else
            return text + at + (unsigned int)__builtin_ctz(mask);
#endif
        }
    }
    return find_sets_scalar(text + at, candidates - at, sets, count);
}

XX_REGEX_AVX2
static const unsigned char *find_sets_avx2(const unsigned char *text, size_t candidates,
                                           const xx_regex_byteset *sets, size_t count)
{
    __m256i low[XX_REGEX_MAX_SETS], high[XX_REGEX_MAX_SETS];
    __m256i b0[XX_REGEX_MAX_SETS], b1[XX_REGEX_MAX_SETS], b2[XX_REGEX_MAX_SETS];
    int kind[XX_REGEX_MAX_SETS];
    const __m256i bits = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)(const void *)re_set_bits));
    const __m256i keep_low = _mm256_set1_epi8(-113 /* 0x8F */), flip = _mm256_set1_epi8(-128 /* 0x80 */);
    const __m256i nibble = _mm256_set1_epi8(0x0F);
    const unsigned char *found = NULL;
    size_t at = 0, j;
    for (j = 0; j < count; ++j) {
        kind[j] = byteset_kind(&sets[j]);
        low[j] = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)(const void *)sets[j].low));
        high[j] = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)(const void *)sets[j].high));
        b0[j] = _mm256_set1_epi8((char)sets[j].bytes[0]);
        b1[j] = _mm256_set1_epi8((char)sets[j].bytes[1]);
        b2[j] = _mm256_set1_epi8((char)sets[j].bytes[2]);
    }
    for (; candidates - at >= 32; at += 32) {
        __m256i all = _mm256_set1_epi8(-1);
        unsigned int mask;
        for (j = 0; j < count; ++j) {
            const __m256i v = _mm256_loadu_si256((const __m256i *)(const void *)(text + at + sets[j].offset));
            __m256i member;
            switch (kind[j]) {
                case RE_SET_EQ1: member = _mm256_cmpeq_epi8(v, b0[j]); break;
                case RE_SET_EQ2: member = _mm256_or_si256(_mm256_cmpeq_epi8(v, b0[j]), _mm256_cmpeq_epi8(v, b1[j])); break;
                case RE_SET_EQ3:
                    member = _mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi8(v, b0[j]), _mm256_cmpeq_epi8(v, b1[j])),
                                             _mm256_cmpeq_epi8(v, b2[j]));
                    break;
                default: {
                    __m256i row = _mm256_shuffle_epi8(low[j], _mm256_and_si256(v, keep_low));
                    const __m256i bit = _mm256_shuffle_epi8(bits, _mm256_and_si256(_mm256_srli_epi16(v, 4), nibble));
                    if (kind[j] == RE_SET_FULL)
                        row = _mm256_or_si256(row, _mm256_shuffle_epi8(high[j], _mm256_and_si256(_mm256_xor_si256(v, flip), keep_low)));
                    member = _mm256_cmpeq_epi8(_mm256_and_si256(row, bit), bit);
                    break;
                }
            }
            all = _mm256_and_si256(all, member);
            /* The first set is the most selective: most blocks end here. */
            if (_mm256_testz_si256(all, all)) break;
        }
        mask = (unsigned int)_mm256_movemask_epi8(all);
        if (mask) {
#if defined(_MSC_VER)
            unsigned long bit;
            _BitScanForward(&bit, mask);
            found = text + at + bit;
#else
            found = text + at + (unsigned int)__builtin_ctz(mask);
#endif
            break;
        }
    }
    _mm256_zeroupper();
    if (found) return found;
    return find_sets_scalar(text + at, candidates - at, sets, count);
}
#endif

const unsigned char *xx_regex_find_sets(const unsigned char *text, size_t candidates,
                                       const xx_regex_byteset *sets, size_t count)
{
    if (!text || !candidates || !sets || !count || count > XX_REGEX_MAX_SETS) return NULL;
#ifdef XX_REGEX_X86
    if (candidates >= 32 && xx_is_avx2_enabled()) return find_sets_avx2(text, candidates, sets, count);
    /* Every SSE4.2 processor implements SSSE3, which xxfclib does not
     * report separately. */
    if (candidates >= 16 && xx_is_sse42_enabled()) return find_sets_ssse3(text, candidates, sets, count);
#endif
    return find_sets_scalar(text, candidates, sets, count);
}
