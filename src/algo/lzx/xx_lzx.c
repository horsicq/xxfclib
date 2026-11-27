/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzx/xx_lzx.h"
#include "xx_lzx_gated.h"
#include "xxfclib/memory/xx_memory.h"
#include <string.h>
#include <stdint.h>

/* Bytes from pointer a up to pointer b (a <= b, same array).  Computed on uintptr_t: a plain pointer
   difference is undefined once it exceeds PTRDIFF_MAX (blocks or outputs over 2 GiB on 32-bit targets). */
#define LZX_SPAN(a, b) ((size_t)((uintptr_t)(b) - (uintptr_t)(a)))

#if defined(_MSC_VER)
#include <intrin.h>
#endif
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define LZX_SSE2 1
#endif
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define LZX_LITTLE_ENDIAN 1
#endif

#define LZX_FRAME_SIZE 32768U
#define LZX_PRETREE_SYMBOLS 20U
#define LZX_LENGTH_SYMBOLS 249U
#define LZX_ALIGNED_SYMBOLS 8U
#define LZX_MAX_MAIN_SYMBOLS 656U
#define LZX_MAX_CODE_LENGTH 16U
#define LZX_BLOCK_VERBATIM 1U
#define LZX_BLOCK_ALIGNED 2U
#define LZX_BLOCK_UNCOMPRESSED 3U
#define LZX_MAX_MATCH 257U

/* Decode tables: a direct table indexed by the next TB bits of the stream. An entry is (symbol << 5) | code length.
   When a code is longer than TB bits, the direct entry is 0x8000 | start of a second-level table, which is indexed
   by the following (longest code - TB) bits. All codes of one prefix share one second-level table, and a prefix that
   has long codes has at least two of them, so a tree of n symbols needs at most n / 2 second-level tables.
   The main tree's entries are 32 bits wide: the upper half of a match symbol's entry says how the offset is read
   (LZX_HIGH_*), so that the decoder knows it as soon as it has the entry, without a second lookup by position slot. */
#define LZX_MAIN_TB 11U
#define LZX_LENGTH_TB 10U
#define LZX_ALIGNED_TB 7U
#define LZX_PRE_TB 8U
#define LZX_SUB_FLAG 0x8000U
#define LZX_MAIN_TABLE_SIZE ((1U << LZX_MAIN_TB) + (LZX_MAX_MAIN_SYMBOLS / 2U) * (1U << (LZX_MAX_CODE_LENGTH - LZX_MAIN_TB)))
#define LZX_LENGTH_TABLE_SIZE ((1U << LZX_LENGTH_TB) + (LZX_LENGTH_SYMBOLS / 2U) * (1U << (LZX_MAX_CODE_LENGTH - LZX_LENGTH_TB)))
#define LZX_ALIGNED_TABLE_SIZE (1U << LZX_ALIGNED_TB)
#define LZX_PRE_TABLE_SIZE ((1U << LZX_PRE_TB) + (LZX_PRETREE_SYMBOLS / 2U) * (1U << (LZX_MAX_CODE_LENGTH - LZX_PRE_TB)))

/* Wide match copies may write up to this many bytes past the end of the match. */
#define LZX_COPY_SLACK 32U

/* Upper half of a main-tree entry of a match symbol: the offset is `LZX_HIGH_BITS` verbatim bits, which are
   followed by an aligned-tree symbol for their three low bits when LZX_HIGH_ALIGNED is set, plus the position slot's
   base. Zero for literals. */
#define LZX_HIGH_BITS 31U
#define LZX_HIGH_ALIGNED 32U

typedef struct lzx_state_s {
    /* Bit reader. `bits` holds `bit_count` valid bits at its top; `in` is the next unread 16-bit word. Bits below
       the valid ones are either zero or equal to the stream bits that follow, never anything else. */
    const uint8_t *in, *in_end;
    uint64_t bits;
    int bit_count;
    bool error;
    bool length_empty;
    uint32_t window_size;
    uint32_t r0, r1, r2;
    unsigned main_symbols, position_slots;
    unsigned main_sub, length_sub, pre_sub;
    uint32_t slot_base[51];                         /* first offset of a position slot, minus 2 */
    uint16_t main_high[2][LZX_MAX_MAIN_SYMBOLS];    /* [0] verbatim blocks, [1] aligned-offset blocks */
    uint8_t main_lengths[LZX_MAX_MAIN_SYMBOLS];
    uint8_t length_lengths[LZX_LENGTH_SYMBOLS];
    uint16_t aligned_table[LZX_ALIGNED_TABLE_SIZE];
    uint16_t pre_table[LZX_PRE_TABLE_SIZE];
    uint16_t length_table[LZX_LENGTH_TABLE_SIZE];
    uint32_t main_table[LZX_MAIN_TABLE_SIZE];
} lzx_state;

/* Four 16-bit little-endian words, the first one in the top 16 bits. p must have 8 readable bytes. */
static inline uint64_t lzx_load_words(const uint8_t *p)
{
#ifdef LZX_LITTLE_ENDIAN
    uint64_t x;
    memcpy(&x, p, sizeof(x));
    x = (x << 32U) | (x >> 32U);
    return ((x & 0x0000ffff0000ffffULL) << 16U) | ((x >> 16U) & 0x0000ffff0000ffffULL);
#else
    return ((uint64_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8U)) << 48U) | ((uint64_t)((uint32_t)p[2] | ((uint32_t)p[3] << 8U)) << 32U) |
           ((uint64_t)((uint32_t)p[4] | ((uint32_t)p[5] << 8U)) << 16U) | (uint64_t)((uint32_t)p[6] | ((uint32_t)p[7] << 8U));
#endif
}

/* Loads whole words while they fit: afterwards at least 48 bits are valid, or every whole word of the input has been
   loaded. Needs 0 <= cnt <= 63. A trailing odd byte is never loaded as valid bits. */
#define LZX_REFILL(in, in_end, buf, cnt)                                            \
    do {                                                                            \
        if (LZX_SPAN((in), (in_end)) >= 8U) {                                      \
            unsigned refill_words = (63U - (unsigned)(cnt)) >> 4U;                  \
            (buf) |= lzx_load_words(in) >> (cnt);                                   \
            (in) += refill_words * 2U;                                              \
            (cnt) += (int)(refill_words * 16U);                                     \
        } else {                                                                    \
            while ((cnt) <= 48 && LZX_SPAN((in), (in_end)) >= 2U) {                \
                (buf) |= (uint64_t)((uint32_t)(in)[0] | ((uint32_t)(in)[1] << 8U)) << (48 - (cnt)); \
                (in) += 2;                                                          \
                (cnt) += 16;                                                        \
            }                                                                       \
        }                                                                           \
    } while (0)

/* Decodes one symbol into `e` (table entry: symbol << 5 | length) and consumes its bits. The caller checks cnt < 0
   afterwards when the input may be exhausted. */
#define LZX_DECODE(e, table, tb, sub, buf, cnt)                                     \
    do {                                                                            \
        (e) = (table)[(size_t)((buf) >> (64U - (tb)))];                             \
        if ((e) & LZX_SUB_FLAG) (e) = (table)[((e) & 0x7fffU) + (size_t)(((buf) << (tb)) >> (64U - (sub)))]; \
        (buf) <<= ((e) & 31U);                                                      \
        (cnt) -= (int)((e) & 31U);                                                  \
    } while (0)

/* Stores `entry` into `span` (a power of two) consecutive table slots. Written out by hand: compilers turn the plain
   loop into a string instruction, which is slow for the short spans that dominate. */
static inline void lzx_fill_entries(uint16_t *p, uint16_t entry, unsigned span)
{
    if (span == 1U) p[0] = entry;
    else if (span == 2U) p[0] = p[1] = entry;
    else {
        const uint64_t pattern = (uint64_t)entry * 0x0001000100010001ULL;
        unsigned j;
        for (j = 0; j < span; j += 4U) memcpy(p + j, &pattern, sizeof(pattern));
    }
}

/* The same for the 32-bit entries of the main table. */
static inline void lzx_fill_entries32(uint32_t *p, uint32_t entry, unsigned span)
{
    if (span == 1U) p[0] = entry;
    else {
        const uint64_t pattern = (uint64_t)entry * 0x0000000100000001ULL;
        unsigned j;
        for (j = 0; j < span; j += 2U) memcpy(p + j, &pattern, sizeof(pattern));
    }
}

/* Stores a table entry `span` times at `index`. A wide table (the main one) also gets the symbol's upper half. */
#define LZX_STORE(index, symbol, length, span)                                                                \
    do {                                                                                                      \
        const unsigned store_entry = ((unsigned)(symbol) << 5U) | (length);                                   \
        if (high) lzx_fill_entries32((uint32_t *)table + (index), ((uint32_t)high[symbol] << 16U) | store_entry, (span)); \
        else lzx_fill_entries((uint16_t *)table + (index), (uint16_t)store_entry, (span));                    \
    } while (0)

static void lzx_bits_init(lzx_state *s, const uint8_t *input, size_t input_size)
{
    s->in = input;
    s->in_end = input + input_size;
    s->bits = 0;
    s->bit_count = 0;
    s->error = false;
}

static uint32_t lzx_read(lzx_state *s, unsigned count)
{
    uint32_t result;
    if (!count || s->error) return 0;
    if (s->bit_count < (int)count) {
        LZX_REFILL(s->in, s->in_end, s->bits, s->bit_count);
        if (s->bit_count < (int)count) {
            s->error = true;
            return 0;
        }
    }
    result = (uint32_t)(s->bits >> (64U - count));
    s->bits <<= count;
    s->bit_count -= (int)count;
    return result;
}

/* Builds the decode table of a complete prefix code (canonical: shorter codes first, then by symbol number).
   Returns false when the lengths do not form a complete code; an all-zero set is reported through *empty and
   accepted only when empty is not NULL. `table` has uint16_t entries when `high` is NULL; otherwise it has uint32_t
   entries and high[symbol] becomes the upper half of the symbol's entries. */
static bool lzx_build_table(void *table, const uint16_t *high, unsigned tb, unsigned *sub_bits, const uint8_t *lengths, unsigned symbol_count, bool *empty)
{
    uint16_t count[LZX_MAX_CODE_LENGTH + 1U], offsets[LZX_MAX_CODE_LENGTH + 2U], sorted[LZX_MAX_MAIN_SYMBOLS];
    uint32_t total = 0, code = 0; /* codes in units of 2^-16 */
    unsigned i, len, max_len = 0, index = 0, direct_len;
    xx_rt_memset(count, 0, sizeof(count));
    for (i = 0; i < symbol_count; ++i) {
        if (lengths[i] > LZX_MAX_CODE_LENGTH) return false;
        ++count[lengths[i]];
    }
    if (count[0] == symbol_count) {
        /* no code at all: every direct entry sends the decoder to its slow path, which reports the error */
        if (!empty || high) return false;
        *empty = true;
        for (i = 0; i < (1U << tb); ++i) ((uint16_t *)table)[i] = LZX_SUB_FLAG;
        return true;
    }
    if (empty) *empty = false;
    offsets[1] = 0;
    for (len = 1; len <= LZX_MAX_CODE_LENGTH; ++len) {
        total += (uint32_t)count[len] << (LZX_MAX_CODE_LENGTH - len);
        offsets[len + 1U] = (uint16_t)(offsets[len] + count[len]);
        if (count[len]) max_len = len;
    }
    if (total != 0x10000U) return false;
    for (i = 0; i < symbol_count; ++i)
        if (lengths[i]) sorted[offsets[lengths[i]]++] = (uint16_t)i;

    direct_len = max_len < tb ? max_len : tb;
    for (len = 1; len <= direct_len; ++len) {
        unsigned n = count[len], span = 1U << (tb - len);
        while (n--) {
            const unsigned symbol = sorted[index++];
            LZX_STORE(code >> (LZX_MAX_CODE_LENGTH - tb), symbol, len, span);
            code += (uint32_t)1U << (LZX_MAX_CODE_LENGTH - len);
        }
    }
    *sub_bits = 1;
    if (max_len > tb) {
        unsigned sb = max_len - tb, next = 1U << tb, base = 0;
        uint32_t last_prefix = 0xffffffffU;
        *sub_bits = sb;
        for (len = tb + 1U; len <= max_len; ++len) {
            unsigned n = count[len], span = 1U << (max_len - len);
            while (n--) {
                const unsigned symbol = sorted[index++];
                uint32_t prefix = code >> (LZX_MAX_CODE_LENGTH - tb);
                if (prefix != last_prefix) {
                    last_prefix = prefix;
                    base = next;
                    next += 1U << sb;
                    if (high) ((uint32_t *)table)[prefix] = LZX_SUB_FLAG | base;
                    else ((uint16_t *)table)[prefix] = (uint16_t)(LZX_SUB_FLAG | base);
                }
                LZX_STORE(base + ((code >> (LZX_MAX_CODE_LENGTH - max_len)) & ((1U << sb) - 1U)), symbol, len, span);
                code += (uint32_t)1U << (LZX_MAX_CODE_LENGTH - len);
            }
        }
    }
    return true;
}

static bool lzx_read_lengths(lzx_state *s, uint8_t *lengths, unsigned first, unsigned last)
{
    uint8_t pre_lengths[LZX_PRETREE_SYMBOLS];
    const uint16_t *table = s->pre_table;
    const uint8_t *in, *in_end = s->in_end;
    uint64_t buf;
    int cnt;
    unsigned i, sub;
    for (i = 0; i < LZX_PRETREE_SYMBOLS; ++i) pre_lengths[i] = (uint8_t)lzx_read(s, 4);
    if (s->error || !lzx_build_table(s->pre_table, NULL, LZX_PRE_TB, &s->pre_sub, pre_lengths, LZX_PRETREE_SYMBOLS, NULL)) return false;
    sub = s->pre_sub;
    in = s->in;
    buf = s->bits;
    cnt = s->bit_count;
    i = first;
    while (i < last) {
        unsigned e, z, n;
        /* at most 15 + 1 + 15 bits per step */
        LZX_REFILL(in, in_end, buf, cnt);
        LZX_DECODE(e, table, LZX_PRE_TB, sub, buf, cnt);
        z = e >> 5U;
        if (z <= 16U) {
            int value = (int)lengths[i] - (int)z;
            if (cnt < 0) return false;
            lengths[i++] = (uint8_t)(value < 0 ? value + 17 : value);
            continue;
        }
        if (z == 17U) {
            n = (unsigned)(buf >> 60U) + 4U;
            buf <<= 4U;
            cnt -= 4;
            if (cnt < 0 || n > last - i) return false;
            while (n--) lengths[i++] = 0;
        } else if (z == 18U) {
            n = (unsigned)(buf >> 59U) + 20U;
            buf <<= 5U;
            cnt -= 5;
            if (cnt < 0 || n > last - i) return false;
            while (n--) lengths[i++] = 0;
        } else {
            int value;
            n = (unsigned)(buf >> 63U) + 4U;
            buf <<= 1U;
            cnt -= 1;
            LZX_DECODE(e, table, LZX_PRE_TB, sub, buf, cnt);
            z = e >> 5U;
            if (cnt < 0 || z > 16U || n > last - i) return false;
            value = (int)lengths[i] - (int)z;
            if (value < 0) value += 17;
            while (n--) lengths[i++] = (uint8_t)value;
        }
    }
    s->in = in;
    s->bits = buf;
    s->bit_count = cnt;
    return true;
}

static void lzx_init_positions(lzx_state *s, unsigned window_bits)
{
    static const uint8_t slots[7] = {30, 32, 34, 36, 38, 42, 50};
    uint32_t base = 0;
    unsigned i;
    s->position_slots = slots[window_bits - 15U];
    for (i = 0; i < s->position_slots; ++i) {
        unsigned extra = i < 4U ? 0U : (i / 2U) - 1U;
        if (extra > 17U) extra = 17U;
        unsigned j;
        /* slots 0..2 are the repeated offsets; their entries read no bits and their base is never used */
        s->slot_base[i] = base - 2U;
        for (j = 0; j < 8U; ++j) {
            s->main_high[0][256U + i * 8U + j] = (uint16_t)extra;
            /* in an aligned-offset block the three low bits of an offset with at least three are an aligned symbol */
            s->main_high[1][256U + i * 8U + j] = (uint16_t)(extra >= 3U ? (extra - 3U) | LZX_HIGH_ALIGNED : extra);
        }
        base += (uint32_t)1U << extra;
    }
}

/* Drops the bits up to the next 16-bit word boundary; they must be zero. With `always`, an already aligned stream
   drops one whole zero word. */
static bool lzx_align_word(lzx_state *s, bool always)
{
    unsigned discard = (unsigned)s->bit_count & 15U;
    if (discard) {
        if ((s->bits >> (64U - discard)) != 0) return false;
        s->bits <<= discard;
        s->bit_count -= (int)discard;
    } else if (always && lzx_read(s, 16) != 0) return false;
    return !s->error;
}

/* Byte position of the first input byte that has not been consumed; the bit reader must be word aligned. */
static const uint8_t *lzx_raw_position(const lzx_state *s)
{
    return s->in - (size_t)(s->bit_count >> 4) * 2U;
}

static inline void lzx_copy16(uint8_t *dst, const uint8_t *src)
{
#ifdef LZX_SSE2
    _mm_storeu_si128((__m128i *)dst, _mm_loadu_si128((const __m128i *)src));
#else
    uint64_t a, b;
    memcpy(&a, src, 8);
    memcpy(&b, src + 8, 8);
    memcpy(dst, &a, 8);
    memcpy(dst + 8, &b, 8);
#endif
}

static inline void lzx_copy8(uint8_t *dst, const uint8_t *src)
{
    uint64_t a;
    memcpy(&a, src, 8);
    memcpy(dst, &a, 8);
}

#if defined(_MSC_VER) && !defined(__clang__)
#define LZX_INLINE static __forceinline
#elif defined(__GNUC__)
#define LZX_INLINE static inline __attribute__((always_inline))
#else
#define LZX_INLINE static inline
#endif

/* Decodes main-tree symbols of a verbatim (`aligned` false) or aligned-offset block until `out` reaches `run_end`,
   which is neither past the end of the block nor past the end of the CFDATA frame; a match that would cross it is an
   error. A match that starts before `wide_end` may be copied with up to LZX_COPY_SLACK bytes of overrun. Returns
   false on any error. `aligned` is a constant in each of the two callers, which gives two copies of the loop; the
   verbatim one has no aligned-tree lookup on its bit reader's dependency chain.
   The match path avoids data-dependent branches: which of its fields a match has is close to random. */
LZX_INLINE bool lzx_decode_run_any(lzx_state *s, uint8_t *output, uint8_t *out, uint8_t *const run_end, const uint8_t *const wide_end, const bool aligned)
{
    const uint32_t *const main_table = s->main_table, *const slot_base = s->slot_base;
    const uint16_t *const length_table = s->length_table, *const aligned_table = s->aligned_table;
    const unsigned main_sub = s->main_sub, length_sub = s->length_sub;
    const size_t window_size = s->window_size;
    const uint8_t *in = s->in, *const in_end = s->in_end;
    uint64_t buf = s->bits;
    int cnt = s->bit_count;
    uint32_t r0 = s->r0, r1 = s->r1, r2 = s->r2;

    while (out < run_end) {
        unsigned e, sym, match_length, slot, length_header, bits, high;
        uint32_t offset, verbatim, previous;
        size_t history;
        const uint8_t *src;
        /* >= 48 bits unless the input is exhausted: two main symbols and a length symbol, 16 bits each at most */
        LZX_REFILL(in, in_end, buf, cnt);
        LZX_DECODE(e, main_table, LZX_MAIN_TB, main_sub, buf, cnt);
        if (e < (256U << 5U)) {
            if (cnt < 0) return false;
            *out++ = (uint8_t)(e >> 5U);
            if (out == run_end) break;
            LZX_DECODE(e, main_table, LZX_MAIN_TB, main_sub, buf, cnt);
            if (e < (256U << 5U)) {
                if (cnt < 0) return false;
                *out++ = (uint8_t)(e >> 5U);
                continue;
            }
        }
        high = e >> 16U;
        sym = ((e & 0xffffU) >> 5U) - 256U;
        slot = sym >> 3U;
        length_header = sym & 7U;
        match_length = length_header + 2U;
        if (length_header == 7U) {
            e = length_table[(size_t)(buf >> (64U - LZX_LENGTH_TB))];
            if (e & LZX_SUB_FLAG) {
                if (s->length_empty) return false;
                e = length_table[(e & 0x7fffU) + (size_t)((buf << LZX_LENGTH_TB) >> (64U - length_sub))];
            }
            buf <<= (e & 31U);
            cnt -= (int)(e & 31U);
            match_length = (e >> 5U) + 9U;
        }
        if (cnt < 0) return false;
        /* >= 48 bits again unless exhausted: at most 17 verbatim bits, or 14 and an aligned symbol of 7 */
        LZX_REFILL(in, in_end, buf, cnt);
        bits = high & LZX_HIGH_BITS;
        verbatim = (uint32_t)((buf >> 1U) >> (63U - bits));
        buf <<= bits;
        cnt -= (int)bits;
        offset = slot_base[slot] + verbatim;
        if (aligned) {
            /* with LZX_HIGH_ALIGNED: the verbatim bits are the offset's high part and an aligned symbol follows */
            const unsigned has_aligned = (high / LZX_HIGH_ALIGNED) & 1U;
            e = (unsigned)aligned_table[(size_t)(buf >> (64U - LZX_ALIGNED_TB))] & (0U - has_aligned);
            buf <<= (e & 31U);
            cnt -= (int)(e & 31U);
            offset = slot_base[slot] + (verbatim << (has_aligned * 3U)) + (e >> 5U);
        }
        if (cnt < 0) return false;
        /* slot 0: R0. slot 1: R1, swapped with R0. slot 2: R2, swapped with R0. Others: the new offset goes in front */
        previous = r0;
        offset = slot == 0U ? r0 : offset;
        offset = slot == 1U ? r1 : offset;
        offset = slot == 2U ? r2 : offset;
        r0 = offset;
        r2 = slot == 2U ? previous : (slot >= 3U ? r1 : r2);
        r1 = (slot == 0U || slot == 2U) ? r1 : previous;
        /* the source must lie inside the window and inside the data already written */
        history = LZX_SPAN(output, out);
        if (history > window_size) history = window_size;
        if ((size_t)offset - 1U >= history || match_length > (size_t)(run_end - out)) return false;
        src = out - offset;
        if (out < wide_end) {
            uint8_t *const end = out + match_length;
            if (offset >= 16U) {
                lzx_copy16(out, src);
                lzx_copy16(out + 16, src + 16);
                if (match_length > 32U) {
                    uint8_t *d = out + 32;
                    src += 32;
                    do {
                        lzx_copy16(d, src);
                        d += 16;
                        src += 16;
                    } while (d < end);
                }
            } else if (offset >= 8U) {
                uint8_t *d = out;
                do {
                    lzx_copy8(d, src);
                    d += 8;
                    src += 8;
                } while (d < end);
            } else if (offset == 1U) {
                const uint64_t fill = (uint64_t)src[0] * 0x0101010101010101ULL;
                uint8_t *d = out;
                do {
                    memcpy(d, &fill, 8);
                    d += 8;
                } while (d < end);
            } else {
                uint8_t *d = out;
                do {
                    *d++ = *src++;
                } while (d < end);
            }
            out = end;
        } else {
            do {
                *out++ = *src++;
            } while (--match_length);
        }
    }
    s->in = in;
    s->bits = buf;
    s->bit_count = cnt;
    s->r0 = r0;
    s->r1 = r1;
    s->r2 = r2;
    return true;
}

static bool lzx_decode_run_verbatim(lzx_state *s, uint8_t *output, uint8_t *out, uint8_t *const run_end, const uint8_t *const wide_end)
{
    return lzx_decode_run_any(s, output, out, run_end, wide_end, false);
}

static bool lzx_decode_run_aligned(lzx_state *s, uint8_t *output, uint8_t *out, uint8_t *const run_end, const uint8_t *const wide_end)
{
    return lzx_decode_run_any(s, output, out, run_end, wide_end, true);
}

/* p points at a 0xe8 byte at output position `position`; translates its four operand bytes when they are in range.
   Always stores, so that the unpredictable outcome is not a branch. */
static inline void lzx_e8_fix(uint8_t *p, size_t position, int32_t file_size)
{
    const uint32_t current = (uint32_t)position; /* below 2^30 + 2^15 */
    const uint32_t value = (uint32_t)p[1] | ((uint32_t)p[2] << 8U) | ((uint32_t)p[3] << 16U) | ((uint32_t)p[4] << 24U);
    const int32_t absolute = (int32_t)value;
    const uint32_t negative = 0U - (uint32_t)(absolute < 0);
    const uint32_t relative = value + ((negative & (uint32_t)file_size) | (~negative & (0U - current)));
    const uint32_t translate = 0U - (uint32_t)(((int64_t)absolute >= -(int64_t)current) & (absolute < file_size));
    const uint32_t result = value ^ ((value ^ relative) & translate);
    p[1] = (uint8_t)result;
    p[2] = (uint8_t)(result >> 8U);
    p[3] = (uint8_t)(result >> 16U);
    p[4] = (uint8_t)(result >> 24U);
}

/* Intel E8 call translation of one frame. A 0xe8 byte in the first frame_size - 10 bytes takes the next four bytes
   as its operand, translated or not, so scanning resumes five bytes later. */
static void lzx_undo_e8(uint8_t *data, size_t frame_offset, size_t frame_size, int32_t file_size)
{
    uint8_t *const p = data + frame_offset;
    size_t i = 0, limit;
    if (!file_size || frame_size <= 10U || frame_offset >= 0x40000000U) return;
    limit = frame_size - 10U;
#if defined(LZX_SSE2) && (defined(_MSC_VER) || defined(__GNUC__))
    {
        const __m128i e8 = _mm_set1_epi8((char)-24);
        while (limit - i >= 16U) {
            unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(p + i)), e8));
            size_t next = i + 16U;
            while (mask) {
                size_t at;
#if defined(_MSC_VER) && !defined(__clang__)
                unsigned long bit;
                _BitScanForward(&bit, mask);
#else
                unsigned bit = (unsigned)__builtin_ctz(mask);
#endif
                at = i + bit;
                lzx_e8_fix(p + at, frame_offset + at, file_size);
                /* the operand bytes are not opcodes; those in the next 16 bytes are skipped through `next` */
                mask &= ~(0x1fU << bit);
                if (at + 5U > next) next = at + 5U;
            }
            i = next;
            if (i >= limit) return;
        }
    }
#endif
    while (i < limit) {
        if (p[i] != 0xe8U) {
            ++i;
            continue;
        }
        lzx_e8_fix(p + i, frame_offset + i, file_size);
        i += 5U;
    }
}

/* True when CFDATA block `entry` can be decoded into the `room` output bytes that are left for it and the blocks
   after it. */
static bool lzx_entry_valid(const uint8_t *const *blocks, const size_t *block_sizes, const size_t *plain_sizes, size_t block_count, size_t entry, size_t room)
{
    return blocks[entry] && block_sizes[entry] && plain_sizes[entry] && plain_sizes[entry] <= LZX_FRAME_SIZE &&
           (entry + 1U >= block_count || plain_sizes[entry] == LZX_FRAME_SIZE) && plain_sizes[entry] <= room;
}

bool xx_lzx_cab_decode(const uint8_t *const *blocks, const size_t *block_sizes, const size_t *plain_sizes, size_t block_count, unsigned window_bits, uint8_t *output,
                       size_t output_size, size_t *written)
{
    return xx_lzx_cab_decode_gated(blocks, block_sizes, plain_sizes, block_count, window_bits, output, output_size, written, NULL, NULL, NULL);
}

void xx_lzx_cab_translate(uint8_t *output, size_t from, size_t to, int32_t translation_size)
{
    while (from < to) {
        size_t n = to - from;
        if (n > LZX_FRAME_SIZE) n = LZX_FRAME_SIZE;
        lzx_undo_e8(output, from, n, translation_size);
        from += n;
    }
}

bool xx_lzx_cab_decode_gated(const uint8_t *const *blocks, const size_t *block_sizes, const size_t *plain_sizes, size_t block_count, unsigned window_bits,
                             uint8_t *output, size_t output_size, size_t *written, xx_lzx_cab_gate_fn gate, void *context, int32_t *translation_size)
{
    lzx_state *s;
    size_t entry, out_pos = 0, block_remaining = 0, e8_done = 0;
    const uint8_t *wide_end;
    unsigned block_type = 0;
    int32_t intel_size = 0;
    bool raw_odd = false, raw_pad_pending = false, ok = false;
    if (written) *written = 0;
    if (!blocks || !block_sizes || !plain_sizes || !block_count || !output || window_bits < 15U || window_bits > 21U) return false;
    if (!gate) {
        for (entry = 0; entry < block_count; ++entry)
            if (!lzx_entry_valid(blocks, block_sizes, plain_sizes, block_count, entry, output_size)) return false;
    }
    s = (lzx_state *)xx_mem_calloc(1U, sizeof(*s));
    if (!s) return false;
    s->window_size = (uint32_t)1U << window_bits;
    s->r0 = s->r1 = s->r2 = 1;
    lzx_init_positions(s, window_bits);
    s->main_symbols = 256U + s->position_slots * 8U;
    s->main_sub = s->length_sub = s->pre_sub = 1;
    s->length_empty = true;
    /* a match starting before wide_end may be copied with up to LZX_COPY_SLACK bytes of overrun */
    wide_end = output + (output_size >= LZX_MAX_MATCH + LZX_COPY_SLACK ? output_size - (LZX_MAX_MATCH + LZX_COPY_SLACK) : 0U);

    for (entry = 0; entry < block_count; ++entry) {
        const size_t entry_start = out_pos;
        size_t entry_target;
        /* With a gate the block's arguments only become valid here. Matches reach at most one window back, so the
           decoder is done with the bytes more than a window behind it, or with those it has translated. */
        if (gate) {
            size_t settled = out_pos > s->window_size ? out_pos - s->window_size : 0U;
            if (intel_size && !translation_size) settled = e8_done;
            if (!gate(context, entry, settled) || !lzx_entry_valid(blocks, block_sizes, plain_sizes, block_count, entry, output_size - out_pos)) goto done;
        }
        entry_target = plain_sizes[entry];
        lzx_bits_init(s, blocks[entry], block_sizes[entry]);
        if (!entry) {
            if (lzx_read(s, 1)) {
                uint32_t high = lzx_read(s, 16), low = lzx_read(s, 16);
                intel_size = (int32_t)((high << 16U) | low);
            }
            if (s->error) goto done;
            if (translation_size) {
                /* the caller translates, behind the decoder */
                *translation_size = intel_size;
                intel_size = 0;
            }
        }
        if (raw_pad_pending) {
            if (s->in[0] != 0) goto done;
            s->in += 1;
            raw_pad_pending = false;
        }
        while (out_pos - entry_start < entry_target) {
            size_t run;
            if (!block_remaining) {
                size_t new_size;
                unsigned i;
                block_type = lzx_read(s, 3);
                new_size = lzx_read(s, 24);
                if (s->error || !new_size || new_size > output_size - out_pos) goto done;
                block_remaining = new_size;
                raw_odd = false;
                if (block_type == LZX_BLOCK_ALIGNED) {
                    uint8_t aligned_lengths[LZX_ALIGNED_SYMBOLS];
                    unsigned unused_sub;
                    for (i = 0; i < LZX_ALIGNED_SYMBOLS; ++i) aligned_lengths[i] = (uint8_t)lzx_read(s, 3);
                    if (s->error || !lzx_build_table(s->aligned_table, NULL, LZX_ALIGNED_TB, &unused_sub, aligned_lengths, LZX_ALIGNED_SYMBOLS, NULL)) goto done;
                }
                if (block_type == LZX_BLOCK_VERBATIM || block_type == LZX_BLOCK_ALIGNED) {
                    if (!lzx_read_lengths(s, s->main_lengths, 0, 256) || !lzx_read_lengths(s, s->main_lengths, 256, s->main_symbols) ||
                        !lzx_build_table(s->main_table, s->main_high[block_type == LZX_BLOCK_ALIGNED], LZX_MAIN_TB, &s->main_sub, s->main_lengths, s->main_symbols, NULL) ||
                        !lzx_read_lengths(s, s->length_lengths, 0, LZX_LENGTH_SYMBOLS) ||
                        !lzx_build_table(s->length_table, NULL, LZX_LENGTH_TB, &s->length_sub, s->length_lengths, LZX_LENGTH_SYMBOLS, &s->length_empty))
                        goto done;
                } else if (block_type == LZX_BLOCK_UNCOMPRESSED) {
                    const uint8_t *p;
                    uint32_t max_repeat = s->window_size - 3U;
                    if (!lzx_align_word(s, true)) goto done;
                    p = lzx_raw_position(s);
                    s->bits = 0;
                    s->bit_count = 0;
                    if (LZX_SPAN(p, s->in_end) < 12U) goto done;
                    s->r0 = (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
                    s->r1 = (uint32_t)p[4] | ((uint32_t)p[5] << 8U) | ((uint32_t)p[6] << 16U) | ((uint32_t)p[7] << 24U);
                    s->r2 = (uint32_t)p[8] | ((uint32_t)p[9] << 8U) | ((uint32_t)p[10] << 16U) | ((uint32_t)p[11] << 24U);
                    if (!s->r0 || s->r0 > max_repeat || !s->r1 || s->r1 > max_repeat || !s->r2 || s->r2 > max_repeat) goto done;
                    s->in = p + 12;
                    raw_odd = (new_size & 1U) != 0;
                } else goto done;
            }

            /* block_remaining never exceeds output_size - out_pos, so neither does the run */
            run = entry_target - (out_pos - entry_start);
            if (run > block_remaining) run = block_remaining;
            if (block_type == LZX_BLOCK_UNCOMPRESSED) {
                /* the bit reader is empty here: s->in is the raw read position */
                if (run > LZX_SPAN(s->in, s->in_end)) goto done;
                xx_rt_memcpy(output + out_pos, s->in, run);
                s->in += run;
                out_pos += run;
                block_remaining -= run;
                if (!block_remaining) {
                    if (raw_odd) {
                        if (s->in < s->in_end) {
                            if (*s->in++ != 0) goto done;
                        } else raw_pad_pending = true;
                    }
                    raw_odd = false;
                }
                continue;
            }
            if (block_type == LZX_BLOCK_ALIGNED ? !lzx_decode_run_aligned(s, output, output + out_pos, output + out_pos + run, wide_end)
                                                : !lzx_decode_run_verbatim(s, output, output + out_pos, output + out_pos + run, wide_end))
                goto done;
            out_pos += run;
            block_remaining -= run;
        }
        /* The end of a CFDATA block is a frame boundary: the bit stream is re-aligned to 16 bits there, and the
           block's input must be used up exactly. */
        if (block_type != LZX_BLOCK_UNCOMPRESSED && !lzx_align_word(s, false)) goto done;
        if (s->error || lzx_raw_position(s) != s->in_end) goto done;
        /* E8 translation runs one window behind the decoder, while the data is still cached: a frame is translated
           once no later match can reach it, because matches must copy the untranslated bytes. */
        if (intel_size) {
            while (out_pos - e8_done >= (size_t)s->window_size + LZX_FRAME_SIZE) {
                lzx_undo_e8(output, e8_done, LZX_FRAME_SIZE, intel_size);
                e8_done += LZX_FRAME_SIZE;
            }
        }
    }
    /* A final odd-sized raw block may end exactly at the CFDATA boundary;
       its otherwise-required zero pad has no following block to protect. */
    if (out_pos != output_size || block_remaining) goto done;
    if (intel_size) {
        size_t frame;
        for (frame = e8_done; frame < output_size; frame += LZX_FRAME_SIZE) {
            size_t n = output_size - frame;
            if (n > LZX_FRAME_SIZE) n = LZX_FRAME_SIZE;
            lzx_undo_e8(output, frame, n, intel_size);
        }
    }
    ok = true;
done:
    xx_mem_free(s);
    if (ok && written) *written = out_pos;
    return ok;
}
