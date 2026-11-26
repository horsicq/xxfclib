/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Independent C11 implementation of the RAR 5.0 (Unpack50) bitstream.  The
 * implementation follows the public clean-room format description maintained
 * by the rar-research project.  RAR filters and malformed-stream behaviour were
 * cross-checked against the Apache-2.0 rars implementation.  No UnRAR-derived
 * source is used here.
 */

#include "xx_rarx_internal.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

/* Multi-threaded block decoding (see the "Parallel decoding" section below).
 * Off by default; the library's profile builds define it to 1. */
#ifndef XX_RARX50_MT
#define XX_RARX50_MT 0
#endif

#if XX_RARX50_MT
#include "xxfclib/threads/xx_threads.h"
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define XX_RARX50_USE_SSE2 1
#endif

#define XX_RARX50_LEVEL_SYMBOLS 20u
#define XX_RARX50_MAIN_SYMBOLS 306u
#define XX_RARX50_DISTANCE_SYMBOLS 64u
#define XX_RARX50_ALIGN_SYMBOLS 16u
#define XX_RARX50_LENGTH_SYMBOLS 44u
#define XX_RARX50_TOTAL_LENGTHS (XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS + XX_RARX50_ALIGN_SYMBOLS + XX_RARX50_LENGTH_SYMBOLS)
#define XX_RARX50_MAX_HUFF_SYMBOLS XX_RARX50_MAIN_SYMBOLS
#define XX_RARX50_MAX_CODE_BITS 15u
#define XX_RARX50_FAST_BITS 12u
#define XX_RARX50_FAST_SIZE (1u << XX_RARX50_FAST_BITS)
#define XX_RARX50_MAX_FILTERS 8192u
#define XX_RARX50_FILTER_BYTES_PER_SLOT 256u
#define XX_RARX50_MAX_FILTER_SIZE (1u << 22)
#define XX_RARX50_X86_FILE_SIZE 0x01000000u
#define XX_RARX50_POLL_MASK 0x3fffu

/* Token stream (see xx_rarx50_tokenize).  One 64-bit word per token:
 *   bits 48..63  number of literals that precede the token (taken in order
 *                from the literal buffer)
 *   bits 45..47  kind
 *   bits 32..44  match length (MATCH, INIT0..INIT3) or filter type and channels
 *   bits  0..31  distance - 1 (MATCH) or filter block offset (FILTER)
 * A FILTER token is followed by one raw word holding the filter length. */
#define XX_RARX50_TOK_MATCH 0u  /* match with an explicit distance and length */
#define XX_RARX50_TOK_INIT0 1u  /* match whose distance is repeated distance 0..3 as it was when the */
#define XX_RARX50_TOK_INIT1 2u  /* block started (the tokenizer did not know it); explicit length */
#define XX_RARX50_TOK_INIT2 3u
#define XX_RARX50_TOK_INIT3 4u
#define XX_RARX50_TOK_LAST 5u   /* repeat of the last match of the previous block: the last length and */
                                /* repeated distance 0 as they were when the block started */
#define XX_RARX50_TOK_FILTER 6u /* filter record */
#define XX_RARX50_TOK_LITS 7u   /* literals only */
#define XX_RARX50_TOKEN(lits, kind, length, low) \
    (((uint64_t)(lits) << 48) | ((uint64_t)(kind) << 45) | ((uint64_t)(length) << 32) | (uint64_t)(uint32_t)(low))

/* Room one tokenizer call needs and may use.  The literals of one call must
 * fit the 16-bit literal count of a token. */
#define XX_RARX50_TOK_MIN_ROOM 16u
#define XX_RARX50_LIT_MIN_ROOM 16u
#define XX_RARX50_LIT_MAX_ROOM 0xff00u
/* Literal buffers carry this many spare bytes so that short literal runs can
 * be copied with fixed-size moves. */
#define XX_RARX50_LIT_SLACK 32u
/* Scratch buffers of the single-threaded token path. */
#define XX_RARX50_ST_TOKENS 4096u
#define XX_RARX50_ST_LITERALS 16384u
/* Tokens resolved, and their match sources prefetched, ahead of the copies. */
#ifndef XX_RARX50_APPLY_CHUNK
#define XX_RARX50_APPLY_CHUNK 64u
#endif

typedef struct xx_rarx50_bits_s {
    const uint8_t *src;
    const uint8_t *src_end;
    uint64_t buf;
    int bits_left;
    size_t bit_position;
    size_t bit_limit;
} xx_rarx50_bits;

typedef struct xx_rarx50_huffman_s {
    uint32_t first_code[XX_RARX50_MAX_CODE_BITS + 1u];
    uint32_t max_code[XX_RARX50_MAX_CODE_BITS + 1u];
    uint16_t first_index[XX_RARX50_MAX_CODE_BITS + 1u];
    uint16_t count[XX_RARX50_MAX_CODE_BITS + 1u];
    uint16_t symbols[XX_RARX50_MAX_HUFF_SYMBOLS];
    /* (symbol << 6) | code length for codes of at most XX_RARX50_FAST_BITS
     * bits, else 0.  The length is in the low six bits so that an entry can
     * serve as a 64-bit shift count as it is. */
    uint16_t fast_table[XX_RARX50_FAST_SIZE];
    uint16_t symbol_count;
} xx_rarx50_huffman;

/* The four code tables of a block. */
typedef struct xx_rarx50_tables_s {
    xx_rarx50_huffman main_table;
    xx_rarx50_huffman distance_table;
    xx_rarx50_huffman align_table;
    xx_rarx50_huffman length_table;
    bool align_mode;
} xx_rarx50_tables;

typedef enum xx_rarx50_filter_type_e {
    XX_RARX50_FILTER_DELTA = 0,
    XX_RARX50_FILTER_E8 = 1,
    XX_RARX50_FILTER_E8E9 = 2,
    XX_RARX50_FILTER_ARM = 3
} xx_rarx50_filter_type;

typedef struct xx_rarx50_filter_s {
    size_t start;
    size_t length;
    uint8_t channels;
    xx_rarx50_filter_type type;
} xx_rarx50_filter;

/* Sequential LZ state of one member: output cursor, repeated distances, last
 * match length and the filters collected so far. */
typedef struct xx_rarx50_lz_s {
    uint8_t *destination;
    size_t destination_size;
    size_t position;
    size_t dist0;
    size_t dist1;
    size_t dist2;
    size_t dist3;
    size_t last_len;
    xx_rarx50_filter *filters;
    size_t filter_count;
    size_t filter_capacity;
} xx_rarx50_lz;

/* Repeated distances and last length as the tokenizer tracks them through a
 * block.  A worker thread does not know the values at the start of the block:
 * a slot then holds XX_RARX50_REP_INITIAL + n, "repeated distance n at the
 * start of the block", until a match with an explicit distance replaces it. */
#define XX_RARX50_REP_INITIAL ((uint64_t)1 << 40)
typedef struct xx_rarx50_reps_s {
    uint64_t slot[4];
    size_t last_len;
    bool last_known;
} xx_rarx50_reps;

struct xx_rarx50_state {
    /* Tables are read into the spare set, which becomes current only when the
     * whole table record was valid: a failed read leaves the state as it was. */
    xx_rarx50_tables tables[2];
    unsigned table_current;
    bool tables_valid;
    size_t repeated_distance[4];
    size_t last_length;
    uint8_t *history;
    size_t history_size;
    size_t history_capacity;
    size_t window_size;
    xx_rarx_chunk_cb chunk_cb;
    void *chunk_user_data;
    size_t stream_pos;
    /* Scratch of the token path, allocated on first use. */
    uint64_t *token_buffer;
    uint8_t *literal_buffer;
};

static bool xx_rarx50_cancelled(xx_pd_struct *progress)
{
    return progress && xx_pd_is_stopped(progress);
}

#if defined(_MSC_VER)
#define XX_FORCE_INLINE static __forceinline
#define XX_NOINLINE static __declspec(noinline)
#define XX_RARX50_BSWAP32(w) _byteswap_ulong(w)
#define XX_RARX50_BSWAP64(w) _byteswap_uint64(w)
#elif defined(__GNUC__) || defined(__clang__)
#define XX_FORCE_INLINE static inline __attribute__((always_inline))
#define XX_NOINLINE static __attribute__((noinline))
#define XX_RARX50_BSWAP32(w) __builtin_bswap32(w)
#define XX_RARX50_BSWAP64(w) __builtin_bswap64(w)
#else
#define XX_FORCE_INLINE static inline
#define XX_NOINLINE static
#define XX_RARX50_BSWAP32(w) \
    ((((uint32_t)(w) & 0xff000000u) >> 24) | (((uint32_t)(w) & 0x00ff0000u) >> 8) | (((uint32_t)(w) & 0x0000ff00u) << 8) | (((uint32_t)(w) & 0x000000ffu) << 24))
#define XX_RARX50_BSWAP64(w) (((uint64_t)XX_RARX50_BSWAP32((uint32_t)(w)) << 32) | (uint64_t)XX_RARX50_BSWAP32((uint32_t)((uint64_t)(w) >> 32)))
#endif

#if defined(XX_RARX50_USE_SSE2)
#define XX_RARX50_PREFETCH(pointer) _mm_prefetch((const char *)(pointer), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
#define XX_RARX50_PREFETCH(pointer) __builtin_prefetch((pointer))
#else
#define XX_RARX50_PREFETCH(pointer) ((void)(pointer))
#endif

static void xx_rarx50_refill_slow(xx_rarx50_bits *bits)
{
    while (bits->bits_left <= 56 && bits->src < bits->src_end) {
        bits->buf |= ((uint64_t)*bits->src++) << (56 - bits->bits_left);
        bits->bits_left += 8;
    }
}

XX_FORCE_INLINE void xx_rarx50_refill(xx_rarx50_bits *bits)
{
    if (bits->src + 4 <= bits->src_end) {
        uint32_t w = xx_mem_load32(bits->src);
        w = XX_RARX50_BSWAP32(w);
        bits->buf |= ((uint64_t)w) << (32 - bits->bits_left);
        bits->src += 4;
        bits->bits_left += 32;
    } else {
        xx_rarx50_refill_slow(bits);
    }
}

/* Start reading a block payload that holds payload_bits bits. */
static void xx_rarx50_bits_init(xx_rarx50_bits *bits, const uint8_t *payload, size_t payload_size, size_t payload_bits)
{
    bits->src = payload;
    bits->src_end = payload + payload_size;
    bits->buf = 0;
    bits->bits_left = 0;
    bits->bit_position = 0;
    bits->bit_limit = payload_bits;
    xx_rarx50_refill(bits);
}

XX_FORCE_INLINE void xx_rarx50_consume_bits(xx_rarx50_bits *bits, unsigned count)
{
    bits->buf <<= count;
    bits->bits_left -= (int)count;
    bits->bit_position += count;
    if (bits->bits_left < 32) {
        xx_rarx50_refill(bits);
    }
}

static inline xx_rarx_status_t xx_rarx50_read_bits(xx_rarx50_bits *bits, unsigned count, uint32_t *value)
{
    if (count == 0) {
        *value = 0;
        return XX_RARX_STATUS_OK;
    }
    if (bits->bit_limit - bits->bit_position < count) {
        return XX_RARX_STATUS_TRUNCATED;
    }
    *value = (uint32_t)(bits->buf >> (64u - count));
    xx_rarx50_consume_bits(bits, count);
    return XX_RARX_STATUS_OK;
}

/* Writes every field a decoder reads, so the table needs no prior clearing.
 * After a failure the table is unusable. */
static xx_rarx_status_t xx_rarx50_build_huffman(xx_rarx50_huffman *table, const uint8_t *lengths, size_t length_count)
{
    uint16_t next_index[XX_RARX50_MAX_CODE_BITS + 1u];
    int32_t available = 1;
    uint32_t code = 0;
    size_t total = 0;
    size_t filled;
    size_t i;
    unsigned length;

    if (!table || !lengths || length_count > XX_RARX50_MAX_HUFF_SYMBOLS) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    memset(table->count, 0, sizeof(table->count));
    table->first_code[0] = 0;
    table->max_code[0] = 0;
    table->first_index[0] = 0;
    table->symbol_count = 0;
    for (i = 0; i < length_count; ++i) {
        if (lengths[i] > XX_RARX50_MAX_CODE_BITS) {
            return XX_RARX_STATUS_CORRUPT;
        }
        if (lengths[i] != 0) {
            table->count[lengths[i]]++;
            total++;
        }
    }
    for (length = 1; length <= XX_RARX50_MAX_CODE_BITS; ++length) {
        available = available * 2 - (int32_t)table->count[length];
        if (available < 0) {
            return XX_RARX_STATUS_CORRUPT;
        }
    }
    if (total > UINT16_MAX) {
        return XX_RARX_STATUS_CORRUPT;
    }
    table->symbol_count = (uint16_t)total;

    total = 0;
    for (length = 1; length <= XX_RARX50_MAX_CODE_BITS; ++length) {
        code = (code + table->count[length - 1u]) << 1;
        table->first_code[length] = code;
        table->first_index[length] = (uint16_t)total;
        next_index[length] = (uint16_t)total;
        total += table->count[length];
        table->max_code[length] = (code + table->count[length]) << (15u - length);
    }
    for (i = 0; i < length_count; ++i) {
        uint8_t symbol_length = lengths[i];
        uint16_t index;
        if (symbol_length == 0) {
            continue;
        }
        index = next_index[symbol_length]++;
        if (index >= table->symbol_count) {
            return XX_RARX_STATUS_CORRUPT;
        }
        table->symbols[index] = (uint16_t)i;
    }

    /* Fast lookup table for codes of at most XX_RARX50_FAST_BITS bits.  The
     * canonical codes are consecutive, so the entries are written from index 0
     * upwards without gaps; whatever follows the last short code is cleared
     * (longer codes and unused code space take the slow path). */
    for (length = 1; length <= XX_RARX50_FAST_BITS; ++length) {
        uint16_t cnt = table->count[length];
        uint32_t first = table->first_code[length];
        uint16_t fidx = table->first_index[length];
        unsigned shift = XX_RARX50_FAST_BITS - length;
        uint32_t step = 1u << shift;
        uint16_t k;

        for (k = 0; k < cnt; ++k) {
            uint32_t sym_code = first + (uint32_t)k;
            uint16_t sym = table->symbols[fidx + k];
            uint16_t cur_entry = (uint16_t)((sym << 6) | length);
            uint32_t base_pattern = sym_code << shift;
            uint32_t p = 0;

#if defined(XX_RARX50_USE_SSE2)
            if (step >= 8) {
                const __m128i cur8 = _mm_set1_epi16((short)cur_entry);
                do {
                    _mm_storeu_si128((__m128i *)(void *)&table->fast_table[base_pattern + p], cur8);
                    p += 8;
                } while (p < step);
            }
#endif
            if (step >= 4) {
                uint32_t cur2 = ((uint32_t)cur_entry << 16) | cur_entry;
                uint64_t cur4 = ((uint64_t)cur2 << 32) | cur2;
                while (p + 4 <= step) {
                    xx_mem_store64(&table->fast_table[base_pattern + p], cur4);
                    p += 4;
                }
            }
            while (p < step) {
                table->fast_table[base_pattern + p] = cur_entry;
                p++;
            }
        }
    }
    filled = (size_t)table->first_code[XX_RARX50_FAST_BITS] + table->count[XX_RARX50_FAST_BITS];
    if (filled < XX_RARX50_FAST_SIZE) {
        memset(table->fast_table + filled, 0, (XX_RARX50_FAST_SIZE - filled) * sizeof(table->fast_table[0]));
    }

    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_decode_huffman_slow(const xx_rarx50_huffman *table, xx_rarx50_bits *bits, unsigned *symbol)
{
    size_t rem_bits = bits->bit_limit > bits->bit_position ? bits->bit_limit - bits->bit_position : 0;
    uint32_t peek15 = (uint32_t)(bits->buf >> (64u - XX_RARX50_MAX_CODE_BITS));
    unsigned length;
    for (length = XX_RARX50_FAST_BITS + 1u; length <= XX_RARX50_MAX_CODE_BITS; ++length) {
        if (table->count[length] != 0 && peek15 < table->max_code[length]) {
            uint32_t code = peek15 >> (XX_RARX50_MAX_CODE_BITS - length);
            uint32_t offset;
            size_t index;
            if (code < table->first_code[length]) {
                continue;
            }
            if (length > rem_bits) {
                return XX_RARX_STATUS_TRUNCATED;
            }
            offset = code - table->first_code[length];
            index = (size_t)table->first_index[length] + offset;
            if (index >= table->symbol_count) {
                return XX_RARX_STATUS_CORRUPT;
            }
            *symbol = table->symbols[index];
            xx_rarx50_consume_bits(bits, length);
            return XX_RARX_STATUS_OK;
        }
    }
    return XX_RARX_STATUS_CORRUPT;
}

/* A code longer than XX_RARX50_FAST_BITS, looked up from the next 15 bits.
 * Returns the fast-table form (symbol << 6) | length, or 0 when no code
 * matches.  The caller checks the length against the bits that remain. */
XX_NOINLINE uint32_t xx_rarx50_huffman_long(const xx_rarx50_huffman *table, uint32_t peek15)
{
    unsigned length;
    for (length = XX_RARX50_FAST_BITS + 1u; length <= XX_RARX50_MAX_CODE_BITS; ++length) {
        if (table->count[length] != 0 && peek15 < table->max_code[length]) {
            uint32_t code = peek15 >> (XX_RARX50_MAX_CODE_BITS - length);
            size_t index;
            if (code < table->first_code[length]) {
                continue;
            }
            index = (size_t)table->first_index[length] + (code - table->first_code[length]);
            if (index >= table->symbol_count) {
                return 0;
            }
            return ((uint32_t)table->symbols[index] << 6) | length;
        }
    }
    return 0;
}

XX_FORCE_INLINE xx_rarx_status_t xx_rarx50_decode_huffman(const xx_rarx50_huffman *table, xx_rarx50_bits *bits, unsigned *symbol)
{
    uint32_t peek = (uint32_t)(bits->buf >> (64u - XX_RARX50_FAST_BITS));
    uint32_t entry = table->fast_table[peek];
    unsigned length = entry & 0x3Fu;
    if (length != 0) {
        *symbol = entry >> 6;
        xx_rarx50_consume_bits(bits, length);
        return XX_RARX_STATUS_OK;
    }
    return xx_rarx50_decode_huffman_slow(table, bits, symbol);
}

/* Reads a table record into `tables`.  After a failure `tables` is unusable. */
static xx_rarx_status_t xx_rarx50_read_tables(xx_rarx50_tables *tables, xx_rarx50_bits *bits)
{
    uint8_t level_lengths[XX_RARX50_LEVEL_SYMBOLS];
    uint8_t lengths[XX_RARX50_TOTAL_LENGTHS];
    xx_rarx50_huffman level_table;
    size_t position = 0;
    size_t i;
    xx_rarx_status_t status;

    memset(level_lengths, 0, sizeof(level_lengths));
    while (position < XX_RARX50_LEVEL_SYMBOLS) {
        uint32_t value;
        status = xx_rarx50_read_bits(bits, 4, &value);
        if (status != XX_RARX_STATUS_OK) return status;
        if (value == 15u) {
            uint32_t zero_count;
            status = xx_rarx50_read_bits(bits, 4, &zero_count);
            if (status != XX_RARX_STATUS_OK) return status;
            if (zero_count == 0) {
                level_lengths[position++] = 15;
            } else {
                size_t count = (size_t)zero_count + 2u;
                if (count > XX_RARX50_LEVEL_SYMBOLS - position) {
                    count = XX_RARX50_LEVEL_SYMBOLS - position;
                }
                position += count;
            }
        } else {
            level_lengths[position++] = (uint8_t)value;
        }
    }
    status = xx_rarx50_build_huffman(&level_table, level_lengths, XX_RARX50_LEVEL_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;

    /* The code lengths, read with the bit reader held in locals.  The steps
     * are those of xx_rarx50_decode_huffman and xx_rarx50_read_bits. */
    {
        const uint16_t *const level_fast = level_table.fast_table;
        uint64_t bit_buf = bits->buf;
        int bits_left = bits->bits_left;
        size_t bit_pos = bits->bit_position;
        const size_t bit_limit = bits->bit_limit;
        const uint8_t *bit_src = bits->src;
        const uint8_t *const bit_src_end = bits->src_end;

#define XX_RARX50_L_TAKE(count)                                            \
    do {                                                                   \
        unsigned _cnt = (unsigned)(count);                                 \
        bit_buf <<= _cnt;                                                  \
        bits_left -= (int)_cnt;                                            \
        bit_pos += _cnt;                                                   \
        if (bits_left < 32) {                                              \
            if (bit_src + 4 <= bit_src_end) {                              \
                uint32_t _w = xx_mem_load32(bit_src);                      \
                _w = XX_RARX50_BSWAP32(_w);                                \
                bit_buf |= ((uint64_t)_w) << (32 - bits_left);             \
                bit_src += 4;                                              \
                bits_left += 32;                                           \
            } else {                                                       \
                while (bits_left <= 56 && bit_src < bit_src_end) {         \
                    bit_buf |= ((uint64_t)*bit_src++) << (56 - bits_left); \
                    bits_left += 8;                                        \
                }                                                          \
            }                                                              \
        }                                                                  \
    } while (0)

        position = 0;
        while (position < XX_RARX50_TOTAL_LENGTHS) {
            const uint32_t entry = level_fast[bit_buf >> (64u - XX_RARX50_FAST_BITS)];
            unsigned symbol;
            unsigned extra_bits;
            size_t count;
            uint32_t extra;
            if ((entry & 0x3Fu) != 0) {
                symbol = entry >> 6;
                XX_RARX50_L_TAKE(entry & 0x3Fu);
            } else {
                bits->buf = bit_buf;
                bits->bits_left = bits_left;
                bits->bit_position = bit_pos;
                bits->src = bit_src;
                status = xx_rarx50_decode_huffman_slow(&level_table, bits, &symbol);
                if (status != XX_RARX_STATUS_OK) return status;
                bit_buf = bits->buf;
                bits_left = bits->bits_left;
                bit_pos = bits->bit_position;
                bit_src = bits->src;
            }
            if (symbol <= 15u) {
                lengths[position++] = (uint8_t)symbol;
                continue;
            }
            if (symbol > 19u) return XX_RARX_STATUS_CORRUPT;
            /* 16, 17: repeat the previous length; 18, 19: a run of zeros.
             * 16 and 18 carry 3 extra bits (3..10), 17 and 19 carry 7 (11..138). */
            if (symbol <= 17u && position == 0) return XX_RARX_STATUS_CORRUPT;
            extra_bits = (symbol & 1u) != 0 ? 7u : 3u;
            if (bit_limit - bit_pos < extra_bits) return XX_RARX_STATUS_TRUNCATED;
            extra = (uint32_t)(bit_buf >> (64u - extra_bits));
            XX_RARX50_L_TAKE(extra_bits);
            count = ((symbol & 1u) != 0 ? 11u : 3u) + (size_t)extra;
            if (count > XX_RARX50_TOTAL_LENGTHS - position) {
                count = XX_RARX50_TOTAL_LENGTHS - position;
            }
            if (symbol <= 17u) {
                memset(lengths + position, lengths[position - 1u], count);
            } else {
                memset(lengths + position, 0, count);
            }
            position += count;
        }
#undef XX_RARX50_L_TAKE

        bits->buf = bit_buf;
        bits->bits_left = bits_left;
        bits->bit_position = bit_pos;
        bits->src = bit_src;
    }

    status = xx_rarx50_build_huffman(&tables->main_table, lengths, XX_RARX50_MAIN_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_build_huffman(&tables->distance_table, lengths + XX_RARX50_MAIN_SYMBOLS, XX_RARX50_DISTANCE_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_build_huffman(&tables->align_table, lengths + XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS, XX_RARX50_ALIGN_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_build_huffman(&tables->length_table, lengths + XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS + XX_RARX50_ALIGN_SYMBOLS, XX_RARX50_LENGTH_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;

    tables->align_mode = false;
    for (i = XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS; i < XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS + XX_RARX50_ALIGN_SYMBOLS; ++i) {
        if (lengths[i] != 0 && lengths[i] != 4) {
            tables->align_mode = true;
            break;
        }
    }
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_read_filter_integer(xx_rarx50_bits *bits, uint32_t *value)
{
    uint32_t width_code;
    uint32_t result = 0;
    unsigned i;
    xx_rarx_status_t status = xx_rarx50_read_bits(bits, 2, &width_code);
    if (status != XX_RARX_STATUS_OK) return status;
    for (i = 0; i <= width_code; ++i) {
        uint32_t byte_value;
        status = xx_rarx50_read_bits(bits, 8, &byte_value);
        if (status != XX_RARX_STATUS_OK) return status;
        result |= byte_value << (i * 8u);
    }
    *value = result;
    return XX_RARX_STATUS_OK;
}

/* Appends one filter; the array grows geometrically. */
static xx_rarx_status_t xx_rarx50_push_filter(xx_rarx50_lz *lz, const xx_rarx50_filter *filter)
{
    if (lz->filter_count == lz->filter_capacity) {
        size_t new_capacity = lz->filter_capacity == 0 ? 16u : lz->filter_capacity * 2u;
        xx_rarx50_filter *resized;
        if (new_capacity <= lz->filter_capacity || new_capacity > SIZE_MAX / sizeof(*lz->filters)) {
            return XX_RARX_STATUS_LIMIT;
        }
        resized = (xx_rarx50_filter *)xx_rarx_clear_resize(lz->filters, lz->filter_capacity * sizeof(*lz->filters), new_capacity * sizeof(*lz->filters));
        if (!resized) {
            return XX_RARX_STATUS_NO_MEMORY;
        }
        lz->filters = resized;
        lz->filter_capacity = new_capacity;
    }
    lz->filters[lz->filter_count] = *filter;
    lz->filter_count++;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_append_filter(xx_rarx50_lz *lz, size_t current_position, xx_rarx50_bits *bits)
{
    const size_t output_size = lz->destination_size;
    xx_rarx50_filter filter;
    uint32_t offset;
    uint32_t length;
    uint32_t type;
    xx_rarx_status_t status;

    /* Filters are applied once the whole member is decoded, so the bound
     * covers every filter of the member, not only the pending ones as in a
     * streaming decoder.  Scale it with the output: a large solid member of
     * many executables carries far more than 8192 x86 filters. */
    if (lz->filter_count >= XX_RARX50_MAX_FILTERS + output_size / XX_RARX50_FILTER_BYTES_PER_SLOT) {
        return XX_RARX_STATUS_LIMIT;
    }
    status = xx_rarx50_read_filter_integer(bits, &offset);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_read_filter_integer(bits, &length);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_read_bits(bits, 3, &type);
    if (status != XX_RARX_STATUS_OK) return status;
    if (type > XX_RARX50_FILTER_ARM) {
        return XX_RARX_STATUS_UNSUPPORTED_FILTER;
    }
    if (length == 0 || length > XX_RARX50_MAX_FILTER_SIZE) {
        return XX_RARX_STATUS_LIMIT;
    }
    if ((size_t)offset > SIZE_MAX - current_position) {
        return XX_RARX_STATUS_CORRUPT;
    }
    filter.start = current_position + (size_t)offset;
    filter.length = (size_t)length;
    if (filter.start > output_size || filter.length > output_size - filter.start) {
        return XX_RARX_STATUS_CORRUPT;
    }
    if (lz->filter_count != 0) {
        const xx_rarx50_filter *previous = &lz->filters[lz->filter_count - 1u];
        if (previous->length > SIZE_MAX - previous->start || filter.start < previous->start + previous->length) {
            return XX_RARX_STATUS_CORRUPT;
        }
    }
    filter.type = (xx_rarx50_filter_type)type;
    filter.channels = 0;
    if (filter.type == XX_RARX50_FILTER_DELTA) {
        uint32_t channels;
        status = xx_rarx50_read_bits(bits, 5, &channels);
        if (status != XX_RARX_STATUS_OK) return status;
        filter.channels = (uint8_t)(channels + 1u);
    }
    return xx_rarx50_push_filter(lz, &filter);
}

/* Filter record of the token path: only what does not depend on the output
 * position is checked here; xx_rarx50_add_filter does the rest when the
 * tokens are applied.  false: the block must be decoded by the reference
 * decoder, which reports the exact error. */
static bool xx_rarx50_parse_filter(xx_rarx50_bits *bits, unsigned literals, uint64_t *words)
{
    uint32_t offset;
    uint32_t length;
    uint32_t type;
    uint32_t channels = 0;

    if (xx_rarx50_read_filter_integer(bits, &offset) != XX_RARX_STATUS_OK) return false;
    if (xx_rarx50_read_filter_integer(bits, &length) != XX_RARX_STATUS_OK) return false;
    if (xx_rarx50_read_bits(bits, 3, &type) != XX_RARX_STATUS_OK) return false;
    if (type > XX_RARX50_FILTER_ARM) return false;
    if (length == 0 || length > XX_RARX50_MAX_FILTER_SIZE) return false;
    if (type == XX_RARX50_FILTER_DELTA) {
        if (xx_rarx50_read_bits(bits, 5, &channels) != XX_RARX_STATUS_OK) return false;
        channels += 1u;
    }
    words[0] = XX_RARX50_TOKEN(literals, XX_RARX50_TOK_FILTER, type | (channels << 2), offset);
    words[1] = length;
    return true;
}

/* The checks of xx_rarx50_append_filter that depend on the output position. */
static bool xx_rarx50_add_filter(xx_rarx50_lz *lz, size_t current_position, uint32_t offset, uint32_t length, uint32_t type_and_channels)
{
    const size_t output_size = lz->destination_size;
    xx_rarx50_filter filter;

    if (lz->filter_count >= XX_RARX50_MAX_FILTERS + output_size / XX_RARX50_FILTER_BYTES_PER_SLOT) return false;
    if ((size_t)offset > SIZE_MAX - current_position) return false;
    filter.start = current_position + (size_t)offset;
    filter.length = (size_t)length;
    if (filter.start > output_size || filter.length > output_size - filter.start) return false;
    if (lz->filter_count != 0) {
        const xx_rarx50_filter *previous = &lz->filters[lz->filter_count - 1u];
        if (previous->length > SIZE_MAX - previous->start || filter.start < previous->start + previous->length) return false;
    }
    filter.type = (xx_rarx50_filter_type)(type_and_channels & 3u);
    filter.channels = (uint8_t)(type_and_channels >> 2);
    return xx_rarx50_push_filter(lz, &filter) == XX_RARX_STATUS_OK;
}

static const uint8_t xx_rarx50_length_bits[44] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4,
                                                  4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9};
static const uint32_t xx_rarx50_length_base[44] = {2,  3,   4,   5,   6,   7,   8,   9,   10,  12,  14,  16,  18,  22,  26,   30,   34,   42,   50,   58,   66,   82,
                                                   98, 114, 130, 162, 194, 226, 258, 322, 386, 450, 514, 642, 770, 898, 1026, 1282, 1538, 1794, 2050, 2562, 3074, 3586};
static const uint8_t xx_rarx50_dist_bits[64] = {0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9,
                                                10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20,
                                                21, 21, 22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30};
static const uint32_t xx_rarx50_dist_base[64] = {
    1,        2,         3,         4,         5,         7,         9,         13,        17,         25,         33,         49,        65,
    97,       129,       193,       257,       385,       513,       769,       1025,      1537,       2049,       3073,       4097,      6145,
    8193,     12289,     16385,     24577,     32769,     49153,     65537,     98305,     131073,     196609,     262145,     393217,    524289,
    786433,   1048577,   1572865,   2097153,   3145729,   4194305,   6291457,   8388609,   12582913,   16777217,   25165825,   33554433,  50331649,
    67108865, 100663297, 134217729, 201326593, 268435457, 402653185, 536870913, 805306369, 1073741825, 1610612737, 2147483649, 3221225473};

XX_FORCE_INLINE xx_rarx_status_t xx_rarx50_length_from_slot(unsigned slot, xx_rarx50_bits *bits, size_t *length)
{
    if (slot >= XX_RARX50_LENGTH_SYMBOLS) return XX_RARX_STATUS_CORRUPT;
    unsigned bit_count = xx_rarx50_length_bits[slot];
    if (bit_count == 0) {
        *length = (size_t)xx_rarx50_length_base[slot];
        return XX_RARX_STATUS_OK;
    }
    uint32_t extra = (uint32_t)(bits->buf >> (64u - bit_count));
    xx_rarx50_consume_bits(bits, bit_count);
    *length = (size_t)xx_rarx50_length_base[slot] + extra;
    return XX_RARX_STATUS_OK;
}

XX_FORCE_INLINE xx_rarx_status_t xx_rarx50_distance_from_slot(unsigned slot, const xx_rarx50_huffman *align_table, bool align_mode, xx_rarx50_bits *bits,
                                                              size_t *distance)
{
    if (slot >= XX_RARX50_DISTANCE_SYMBOLS) return XX_RARX_STATUS_CORRUPT;
    unsigned bit_count = xx_rarx50_dist_bits[slot];
    if (bit_count == 0) {
        *distance = (size_t)xx_rarx50_dist_base[slot];
        return XX_RARX_STATUS_OK;
    }
    uint32_t extra;
    if (align_mode && bit_count >= 4u) {
        uint32_t high = 0;
        if (bit_count > 4u) {
            high = (uint32_t)(bits->buf >> (64u - (bit_count - 4u)));
            xx_rarx50_consume_bits(bits, bit_count - 4u);
        }
        unsigned low;
        xx_rarx_status_t status = xx_rarx50_decode_huffman(align_table, bits, &low);
        if (status != XX_RARX_STATUS_OK || low >= XX_RARX50_ALIGN_SYMBOLS) return XX_RARX_STATUS_CORRUPT;
        extra = (high << 4) | (uint32_t)low;
    } else {
        extra = (uint32_t)(bits->buf >> (64u - bit_count));
        xx_rarx50_consume_bits(bits, bit_count);
    }
    *distance = (size_t)xx_rarx50_dist_base[slot] + extra;
    return XX_RARX_STATUS_OK;
}

XX_FORCE_INLINE size_t xx_rarx50_length_bonus(size_t distance)
{
    return (size_t)(distance > 0x100u) + (size_t)(distance > 0x2000u) + (size_t)(distance > 0x40000u);
}

static inline xx_rarx_status_t xx_rarx50_copy_match(const xx_rarx50_state *state, uint8_t *destination, size_t *destination_position, size_t destination_size,
                                                    size_t window_size, size_t distance, size_t length)
{
    size_t output_position;
    size_t available;

    if (!state || !destination_position || *destination_position > destination_size || length > destination_size - *destination_position) {
        return XX_RARX_STATUS_CORRUPT;
    }
    output_position = *destination_position;
    if (state->history_size > SIZE_MAX - output_position) {
        return XX_RARX_STATUS_CORRUPT;
    }
    available = state->history_size + output_position;

    if (distance == 0 || distance > window_size || distance > available) {
        if (length != 0) {
            xx_mem_zero(destination + output_position, length);
        }
        *destination_position += length;
        return XX_RARX_STATUS_OK;
    }

    if (length == 0) {
        return XX_RARX_STATUS_OK;
    }

    if (distance <= output_position) {
        /* Entire match is within the current destination buffer */
        uint8_t *dst = destination + output_position;
        const uint8_t *src = dst - distance;
        *destination_position += length;

        if (distance == 1) {
            uint8_t b = *src;
            uint64_t pattern = (uint64_t)b * UINT64_C(0x0101010101010101);
            size_t rem = length;
            while (rem >= 8) {
                xx_mem_store64(dst, pattern);
                dst += 8;
                rem -= 8;
            }
            while (rem > 0) {
                *dst++ = b;
                rem--;
            }
#if defined(XX_RARX50_USE_SSE2)
        } else if (distance >= 16) {
            size_t rem = length;
            while (rem >= 16) {
                _mm_storeu_si128((__m128i *)(void *)dst, _mm_loadu_si128((const __m128i *)(const void *)src));
                dst += 16;
                src += 16;
                rem -= 16;
            }
            if (rem >= 8) {
                memcpy(dst, src, 8);
                dst += 8;
                src += 8;
                rem -= 8;
            }
            while (rem > 0) {
                *dst++ = *src++;
                rem--;
            }
#endif
        } else if (distance >= 8) {
            size_t rem = length;
            while (rem >= 16) {
                memcpy(dst, src, 8);
                memcpy(dst + 8, src + 8, 8);
                dst += 16;
                src += 16;
                rem -= 16;
            }
            if (rem >= 8) {
                memcpy(dst, src, 8);
                dst += 8;
                src += 8;
                rem -= 8;
            }
            while (rem > 0) {
                *dst++ = *src++;
                rem--;
            }
        } else {
            size_t rem = length;
            while (rem > 0) {
                *dst++ = *src++;
                rem--;
            }
        }
        return XX_RARX_STATUS_OK;
    } else {
        /* Match starts in history buffer */
        size_t from_history = distance - output_position;
        size_t hist_offset = state->history_size - from_history;
        uint8_t *dst = destination + output_position;

        if (from_history >= length) {
            /* Entire match is within history */
            const uint8_t *hsrc = state->history + hist_offset;
            size_t rem = length;
            while (rem >= 8) {
                xx_mem_copy64(dst, hsrc);
                dst += 8;
                hsrc += 8;
                rem -= 8;
            }
            while (rem > 0) {
                *dst++ = *hsrc++;
                rem--;
            }
        } else {
            /* First part from history, rest from destination */
            size_t from_dest = length - from_history;
            const uint8_t *hsrc = state->history + hist_offset;
            size_t rem = from_history;
            while (rem >= 8) {
                xx_mem_copy64(dst, hsrc);
                dst += 8;
                hsrc += 8;
                rem -= 8;
            }
            while (rem > 0) {
                *dst++ = *hsrc++;
                rem--;
            }
            if (distance >= 8) {
                const uint8_t *src = dst - distance;
                while (from_dest >= 8) {
                    memcpy(dst, src, 8);
                    dst += 8;
                    src += 8;
                    from_dest -= 8;
                }
                while (from_dest > 0) {
                    *dst++ = *src++;
                    from_dest--;
                }
            } else {
                size_t i;
                for (i = 0; i < from_dest; ++i) {
                    dst[i] = dst[i - distance];
                }
            }
        }
    }

    *destination_position += length;
    return XX_RARX_STATUS_OK;
}

/* Individual word copies must retain byte-range alias dependencies when
 * distance is between one and two word widths. Standard memcpy preserves
 * the ordering of these nonoverlapping individual steps under optimization. */
XX_FORCE_INLINE void xx_rarx50_copy_match_fast_ptr(uint8_t *dst, size_t distance, size_t length, const uint8_t *dst_end)
{
    const uint8_t *src = dst - distance;
    if (dst + length + 16u <= dst_end) {
#if defined(XX_RARX50_USE_SSE2)
        if (distance >= 16) {
            _mm_storeu_si128((__m128i *)(void *)dst, _mm_loadu_si128((const __m128i *)(const void *)src));
            if (length <= 16) return;
            uint8_t *lim = dst + length;
            do {
                dst += 16;
                src += 16;
                _mm_storeu_si128((__m128i *)(void *)dst, _mm_loadu_si128((const __m128i *)(const void *)src));
            } while (dst + 16 < lim);
            return;
        }
#endif
        if (distance >= 8) {
            memcpy(dst, src, 8);
            if (length <= 8) return;
            memcpy(dst + 8, src + 8, 8);
            if (length <= 16) return;
            uint8_t *lim = dst + length;
            dst += 16;
            src += 16;
            while (dst < lim) {
                memcpy(dst, src, 8);
                memcpy(dst + 8, src + 8, 8);
                dst += 16;
                src += 16;
            }
            return;
        }
        uint8_t *lim = dst + length;
        if (distance >= 4) {
            do {
                memcpy(dst, src, 4);
                dst += 4;
                src += 4;
            } while (dst < lim);
        } else if (distance == 1) {
            uint8_t b = *src;
            uint64_t pattern = (uint64_t)b * UINT64_C(0x0101010101010101);
            do {
                xx_mem_store64(dst, pattern);
                dst += 8;
            } while (dst < lim);
        } else if (distance == 2) {
            uint16_t b2 = (uint16_t)src[0] | ((uint16_t)src[1] << 8);
            uint32_t b4 = (uint32_t)b2 | ((uint32_t)b2 << 16);
            uint64_t pattern = (uint64_t)b4 | ((uint64_t)b4 << 32);
            do {
                xx_mem_store64(dst, pattern);
                dst += 8;
            } while (dst < lim);
        } else {
            do {
                *dst++ = *src++;
            } while (dst < lim);
        }
    } else {
        size_t rem = length;
        while (rem > 0) {
            *dst++ = *src++;
            rem--;
        }
    }
}

static xx_rarx_status_t xx_rarx50_remember_history(xx_rarx50_state *state, const uint8_t *data, size_t data_size, size_t window_size)
{
    size_t old_keep;
    size_t data_keep;
    size_t wanted;
    uint8_t *resized;

    if (!state || (!data && data_size != 0) || window_size == 0) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    data_keep = data_size < window_size ? data_size : window_size;
    old_keep = window_size - data_keep;
    if (old_keep > state->history_size) old_keep = state->history_size;
    wanted = old_keep + data_keep;
    if (wanted > state->history_capacity) {
        resized = (uint8_t *)xx_rarx_clear_resize(state->history, state->history_capacity, wanted);
        if (!resized && wanted != 0) {
            return XX_RARX_STATUS_NO_MEMORY;
        }
        state->history = resized;
        state->history_capacity = wanted;
    }
    if (old_keep != 0 && old_keep != state->history_size) {
        xx_mem_move(state->history, state->history + state->history_size - old_keep, old_keep);
    }
    if (data_keep != 0) {
        xx_mem_copy(state->history + old_keep, data + data_size - data_keep, data_keep);
    }
    state->history_size = wanted;
    if (wanted < state->history_capacity) xx_mem_zero(state->history + wanted, state->history_capacity - wanted);
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_filter_delta(uint8_t *data, size_t size, unsigned channels, xx_pd_struct *progress)
{
    uint8_t *decoded;
    size_t source_position = 0;
    unsigned channel;

    if (!data || channels == 0 || channels > 32u) {
        return XX_RARX_STATUS_CORRUPT;
    }
    if (size == 0) return XX_RARX_STATUS_OK;

    if (channels == 1u) {
        uint8_t previous = 0;
        size_t position;
        for (position = 0; position < size; ++position) {
            if ((position & XX_RARX50_POLL_MASK) == 0 && xx_rarx50_cancelled(progress)) {
                return XX_RARX_STATUS_CANCELLED;
            }
            previous = (uint8_t)(previous - data[position]);
            data[position] = previous;
        }
        return XX_RARX_STATUS_OK;
    }

    decoded = (uint8_t *)xx_mem_alloc(size);
    if (!decoded) return XX_RARX_STATUS_NO_MEMORY;
    for (channel = 0; channel < channels; ++channel) {
        uint8_t previous = 0;
        size_t position;
        for (position = channel; position < size; position += channels) {
            if ((source_position & XX_RARX50_POLL_MASK) == 0 && xx_rarx50_cancelled(progress)) {
                xx_rarx_clear_free(decoded, size);
                return XX_RARX_STATUS_CANCELLED;
            }
            if (source_position >= size) {
                xx_rarx_clear_free(decoded, size);
                return XX_RARX_STATUS_CORRUPT;
            }
            previous = (uint8_t)(previous - data[source_position++]);
            decoded[position] = previous;
        }
    }
    if (source_position != size) {
        xx_rarx_clear_free(decoded, size);
        return XX_RARX_STATUS_CORRUPT;
    }
    xx_mem_copy(data, decoded, size);
    xx_rarx_clear_free(decoded, size);
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_filter_x86(uint8_t *data, size_t size, uint32_t file_offset, bool include_e9, xx_pd_struct *progress)
{
    size_t position = 0;

#if defined(XX_RARX50_USE_SSE2)
    const __m128i v_e8 = _mm_set1_epi8((char)-24); /* 0xe8 */
    const __m128i v_e9 = _mm_set1_epi8((char)-23); /* 0xe9 */

    while (position + 16u + 4u <= size) {
        if ((position & XX_RARX50_POLL_MASK) == 0 && xx_rarx50_cancelled(progress)) {
            return XX_RARX_STATUS_CANCELLED;
        }
        __m128i chunk = _mm_loadu_si128((const __m128i *)(const void *)(data + position));
        __m128i cmp = _mm_cmpeq_epi8(chunk, v_e8);
        if (include_e9) {
            cmp = _mm_or_si128(cmp, _mm_cmpeq_epi8(chunk, v_e9));
        }
        int mask = _mm_movemask_epi8(cmp);
        if (mask == 0) {
            position += 16u;
            continue;
        }
        unsigned long bit_idx;
#if defined(_MSC_VER)
        _BitScanForward(&bit_idx, (unsigned long)mask);
#elif defined(__GNUC__) || defined(__clang__)
        bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask);
#else
        for (bit_idx = 0; bit_idx < 16; ++bit_idx) {
            if (mask & (1 << bit_idx)) break;
        }
#endif
        position += bit_idx;
        size_t address_position = position + 1u;
        uint32_t offset = (file_offset + (uint32_t)address_position) & (XX_RARX50_X86_FILE_SIZE - 1u);
        uint32_t address = xx_mem_load32(data + address_position);
        if (address < XX_RARX50_X86_FILE_SIZE) {
            address -= offset;
            xx_mem_store32(data + address_position, address);
        } else if (address > UINT32_MAX - offset) {
            address += XX_RARX50_X86_FILE_SIZE;
            xx_mem_store32(data + address_position, address);
        }
        position += 5u;
    }
#endif

    while (position + 4u < size) {
        uint8_t opcode;
        if ((position & XX_RARX50_POLL_MASK) == 0 && xx_rarx50_cancelled(progress)) {
            return XX_RARX_STATUS_CANCELLED;
        }
        opcode = data[position];
        if (opcode == 0xe8u || (include_e9 && opcode == 0xe9u)) {
            size_t address_position = position + 1u;
            uint32_t offset = (file_offset + (uint32_t)address_position) & (XX_RARX50_X86_FILE_SIZE - 1u);
            uint32_t address = xx_mem_load32(data + address_position);
            if (address < XX_RARX50_X86_FILE_SIZE) {
                address -= offset;
                xx_mem_store32(data + address_position, address);
            } else if (address > UINT32_MAX - offset) {
                address += XX_RARX50_X86_FILE_SIZE;
                xx_mem_store32(data + address_position, address);
            }
            position += 5u;
        } else {
            position++;
        }
    }
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_filter_arm(uint8_t *data, size_t size, uint32_t file_offset, xx_pd_struct *progress)
{
    size_t position = 0;

#if defined(XX_RARX50_USE_SSE2)
    const __m128i v_eb = _mm_set1_epi8((char)-21) /* 0xeb */;
    while (position + 16u <= size) {
        if ((position & XX_RARX50_POLL_MASK) == 0 && xx_rarx50_cancelled(progress)) {
            return XX_RARX_STATUS_CANCELLED;
        }
        __m128i chunk = _mm_loadu_si128((const __m128i *)(const void *)(data + position));
        __m128i cmp = _mm_cmpeq_epi8(chunk, v_eb);
        int mask = _mm_movemask_epi8(cmp) & 0x8888;
        if (mask == 0) {
            position += 16u;
            continue;
        }
        for (size_t step = 0; step < 16u; step += 4u) {
            if (data[position + step + 3u] == 0xebu) {
                uint32_t offset = (file_offset + (uint32_t)(position + step)) >> 2;
                uint32_t value = (uint32_t)data[position + step] | ((uint32_t)data[position + step + 1u] << 8) | ((uint32_t)data[position + step + 2u] << 16);
                value = (value - offset) & 0x00ffffffu;
                data[position + step] = (uint8_t)value;
                data[position + step + 1u] = (uint8_t)(value >> 8);
                data[position + step + 2u] = (uint8_t)(value >> 16);
            }
        }
        position += 16u;
    }
#endif

    for (; position + 3u < size; position += 4u) {
        uint32_t value;
        uint32_t offset;
        if ((position & XX_RARX50_POLL_MASK) == 0 && xx_rarx50_cancelled(progress)) {
            return XX_RARX_STATUS_CANCELLED;
        }
        if (data[position + 3u] != 0xebu) continue;
        value = (uint32_t)data[position] | ((uint32_t)data[position + 1u] << 8) | ((uint32_t)data[position + 2u] << 16);
        offset = (file_offset + (uint32_t)position) >> 2;
        value = (value - offset) & 0x00ffffffu;
        data[position] = (uint8_t)value;
        data[position + 1u] = (uint8_t)(value >> 8);
        data[position + 2u] = (uint8_t)(value >> 16);
    }
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rarx50_apply_filters(uint8_t *destination, size_t destination_size, const xx_rarx50_filter *filters, size_t filter_count,
                                                xx_pd_struct *progress)
{
    size_t i;
    for (i = 0; i < filter_count; ++i) {
        const xx_rarx50_filter *filter = &filters[i];
        uint8_t *data;
        xx_rarx_status_t status;
        if (xx_rarx50_cancelled(progress)) {
            return XX_RARX_STATUS_CANCELLED;
        }
        if (filter->start > destination_size || filter->length > destination_size - filter->start) {
            return XX_RARX_STATUS_CORRUPT;
        }
        data = destination + filter->start;
        switch (filter->type) {
            case XX_RARX50_FILTER_DELTA: status = xx_rarx50_filter_delta(data, filter->length, filter->channels, progress); break;
            case XX_RARX50_FILTER_E8: status = xx_rarx50_filter_x86(data, filter->length, (uint32_t)filter->start, false, progress); break;
            case XX_RARX50_FILTER_E8E9: status = xx_rarx50_filter_x86(data, filter->length, (uint32_t)filter->start, true, progress); break;
            case XX_RARX50_FILTER_ARM: status = xx_rarx50_filter_arm(data, filter->length, (uint32_t)filter->start, progress); break;
            default: status = XX_RARX_STATUS_UNSUPPORTED_FILTER; break;
        }
        if (status != XX_RARX_STATUS_OK) return status;
    }
    return XX_RARX_STATUS_OK;
}

xx_rarx50_state *xx_rarx50_create(void)
{
    return (xx_rarx50_state *)xx_mem_calloc(1, sizeof(xx_rarx50_state));
}

void xx_rarx50_destroy(xx_rarx50_state *state)
{
    if (!state) return;
    xx_mem_free(state->history);
    xx_mem_free(state->token_buffer);
    xx_mem_free(state->literal_buffer);
    xx_mem_free(state);
}

void xx_rarx50_reset(xx_rarx50_state *state)
{
    uint8_t *history;
    size_t capacity;
    xx_rarx_chunk_cb chunk_cb;
    void *chunk_user_data;
    uint64_t *token_buffer;
    uint8_t *literal_buffer;
    if (!state) return;
    history = state->history;
    capacity = state->history_capacity;
    chunk_cb = state->chunk_cb;
    chunk_user_data = state->chunk_user_data;
    token_buffer = state->token_buffer;
    literal_buffer = state->literal_buffer;
    xx_mem_zero(history, capacity);
    memset(state, 0, sizeof(*state));
    state->history = history;
    state->history_capacity = capacity;
    state->chunk_cb = chunk_cb;
    state->chunk_user_data = chunk_user_data;
    state->token_buffer = token_buffer;
    state->literal_buffer = literal_buffer;
    state->stream_pos = 0;
}

void xx_rarx50_set_chunk_callback(xx_rarx50_state *state, xx_rarx_chunk_cb cb, void *user_data)
{
    if (!state) return;
    state->chunk_cb = cb;
    state->chunk_user_data = user_data;
    state->stream_pos = 0;
}

/* The scratch buffers of the token path; false when they cannot be allocated
 * (the reference decoder is used instead). */
static bool xx_rarx50_token_scratch(xx_rarx50_state *state)
{
    if (!state->token_buffer) {
        state->token_buffer = (uint64_t *)xx_mem_alloc((size_t)XX_RARX50_ST_TOKENS * sizeof(uint64_t));
    }
    if (!state->literal_buffer) {
        state->literal_buffer = (uint8_t *)xx_mem_calloc((size_t)XX_RARX50_ST_LITERALS + XX_RARX50_LIT_SLACK, 1);
    }
    return state->token_buffer != NULL && state->literal_buffer != NULL;
}

/* Reference block decoder: decodes straight into the output and reports the
 * exact status of malformed input.  The token path falls back to it. */
XX_NOINLINE xx_rarx_status_t xx_rarx50_decode_block(const xx_rarx50_state *state, const xx_rarx50_tables *tables, xx_rarx50_lz *lz, size_t window_size,
                                                    xx_rarx50_bits *bits)
{
    uint8_t *const destination = lz->destination;
    const size_t destination_size = lz->destination_size;
    uint8_t *dst = destination + lz->position;
    const uint8_t *const dst_end = destination + destination_size;
    size_t dist0 = lz->dist0;
    size_t dist1 = lz->dist1;
    size_t dist2 = lz->dist2;
    size_t dist3 = lz->dist3;
    size_t last_len = lz->last_len;

    const uint16_t *const main_fast = tables->main_table.fast_table;
    const uint16_t *const length_fast = tables->length_table.fast_table;
    const uint16_t *const dist_fast = tables->distance_table.fast_table;
    const uint16_t *const align_fast = tables->align_table.fast_table;
    const bool align_mode = tables->align_mode;

    uint64_t bit_buf = bits->buf;
    int bits_left = bits->bits_left;
    size_t bit_pos = bits->bit_position;
    const size_t bit_limit = bits->bit_limit;
    const uint8_t *bit_src = bits->src;
    const uint8_t *const bit_src_end = bits->src_end;
    xx_rarx_status_t status = XX_RARX_STATUS_OK;

#define XX_RARX50_CONSUME_FAST(count)                      \
    do {                                                   \
        unsigned _cnt = (unsigned)(count);                 \
        bit_buf <<= _cnt;                                  \
        bits_left -= (int)_cnt;                            \
        if (bits_left < 32) {                              \
            uint32_t _w = xx_mem_load32(bit_src);          \
            _w = XX_RARX50_BSWAP32(_w);                    \
            bit_buf |= ((uint64_t)_w) << (32 - bits_left); \
            bit_src += 4;                                  \
            bits_left += 32;                               \
        }                                                  \
    } while (0)

#define XX_RARX50_SYNC_BITS()                                                                                      \
    do {                                                                                                           \
        bit_pos = anchor_bit_pos + ((size_t)(bit_src - anchor_src) * 8u) + (size_t)(anchor_bits_left - bits_left); \
        bits->buf = bit_buf;                                                                                       \
        bits->bits_left = bits_left;                                                                               \
        bits->bit_position = bit_pos;                                                                              \
        bits->src = bit_src;                                                                                       \
    } while (0)

#define XX_RARX50_RESYNC_BITS()       \
    do {                              \
        bit_buf = bits->buf;          \
        bits_left = bits->bits_left;  \
        bit_pos = bits->bit_position; \
        bit_src = bits->src;          \
        anchor_src = bit_src;         \
        anchor_bits_left = bits_left; \
        anchor_bit_pos = bit_pos;     \
    } while (0)

#define XX_RARX50_CONSUME_SAFE(count)                                      \
    do {                                                                   \
        unsigned _cnt = (unsigned)(count);                                 \
        bit_buf <<= _cnt;                                                  \
        bits_left -= (int)_cnt;                                            \
        bit_pos += _cnt;                                                   \
        if (bits_left < 32) {                                              \
            if (bit_src + 4 <= bit_src_end) {                              \
                uint32_t _w = xx_mem_load32(bit_src);                      \
                _w = XX_RARX50_BSWAP32(_w);                                \
                bit_buf |= ((uint64_t)_w) << (32 - bits_left);             \
                bit_src += 4;                                              \
                bits_left += 32;                                           \
            } else {                                                       \
                while (bits_left <= 56 && bit_src < bit_src_end) {         \
                    bit_buf |= ((uint64_t)*bit_src++) << (56 - bits_left); \
                    bits_left += 8;                                        \
                }                                                          \
            }                                                              \
        }                                                                  \
    } while (0)

#define XX_RARX50_PERFORM_MATCH(dist, len)                                                                                      \
    do {                                                                                                                        \
        size_t _distance = (dist);                                                                                              \
        size_t _length = (len);                                                                                                 \
        if (dst + _length <= dst_end && _distance != 0 && _distance <= (size_t)(dst - destination)) {                           \
            xx_rarx50_copy_match_fast_ptr(dst, _distance, _length, dst_end);                                                    \
            dst += _length;                                                                                                     \
        } else {                                                                                                                \
            size_t current_pos = (size_t)(dst - destination);                                                                   \
            status = xx_rarx50_copy_match(state, destination, &current_pos, destination_size, window_size, _distance, _length); \
            if (status != XX_RARX_STATUS_OK) goto done;                                                                         \
            dst = destination + current_pos;                                                                                    \
        }                                                                                                                       \
    } while (0)

    const uint8_t *const fast_src_end = (bit_src_end >= bit_src + 16u) ? (bit_src_end - 16u) : bit_src;
    const uint8_t *const fast_dst_end = (dst_end >= destination + 32u) ? (dst_end - 32u) : destination;
    const uint8_t *anchor_src = bit_src;
    int anchor_bits_left = bits_left;
    size_t anchor_bit_pos = bit_pos;

    /* Fast path: plenty of bits in input and headroom in output */
    while (bit_src <= fast_src_end && dst <= fast_dst_end) {
        uint32_t peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
        uint32_t entry = main_fast[peek];
        unsigned hlen = entry & 0x3Fu;
        unsigned symbol;
        if (hlen != 0) {
            symbol = entry >> 6;
            XX_RARX50_CONSUME_FAST(hlen);
        } else {
            XX_RARX50_SYNC_BITS();
            status = xx_rarx50_decode_huffman_slow(&tables->main_table, bits, &symbol);
            if (status != XX_RARX_STATUS_OK) goto done;
            XX_RARX50_RESYNC_BITS();
        }
        if (symbol < 256u) {
            *dst++ = (uint8_t)symbol;
            continue;
        } else if (symbol == 256u) {
            XX_RARX50_SYNC_BITS();
            status = xx_rarx50_append_filter(lz, (size_t)(dst - destination), bits);
            if (status != XX_RARX_STATUS_OK) goto done;
            XX_RARX50_RESYNC_BITS();
        } else if (symbol == 257u) {
            if (last_len != 0) {
                XX_RARX50_PERFORM_MATCH(dist0, last_len);
            }
        } else if (symbol <= 261u) {
            size_t distance;
            size_t length;
            unsigned length_slot;
            if (symbol == 258u) {
                distance = dist0;
            } else if (symbol == 259u) {
                distance = dist1;
                dist1 = dist0;
            } else if (symbol == 260u) {
                distance = dist2;
                dist2 = dist1;
                dist1 = dist0;
            } else {
                distance = dist3;
                dist3 = dist2;
                dist2 = dist1;
                dist1 = dist0;
            }
            dist0 = distance;
            if (distance == 0) {
                status = XX_RARX_STATUS_CORRUPT;
                goto done;
            }
            peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
            entry = length_fast[peek];
            hlen = entry & 0x3Fu;
            if (hlen != 0) {
                length_slot = entry >> 6;
                XX_RARX50_CONSUME_FAST(hlen);
            } else {
                XX_RARX50_SYNC_BITS();
                status = xx_rarx50_decode_huffman_slow(&tables->length_table, bits, &length_slot);
                if (status != XX_RARX_STATUS_OK) goto done;
                XX_RARX50_RESYNC_BITS();
            }
            if (length_slot < 8u) {
                length = (size_t)length_slot + 2u;
            } else {
                unsigned num_bits = (length_slot - 4u) >> 2;
                uint32_t extra = (uint32_t)(bit_buf >> (64u - num_bits));
                XX_RARX50_CONSUME_FAST(num_bits);
                length = 2u + (((size_t)4u | ((size_t)length_slot & 3u)) << num_bits) + (size_t)extra;
            }
            last_len = length;
            XX_RARX50_PERFORM_MATCH(distance, length);
        } else {
            unsigned length_slot = symbol - 262u;
            unsigned distance_slot;
            size_t distance;
            size_t length;
            size_t bonus;
            if (length_slot < 8u) {
                length = (size_t)length_slot + 2u;
            } else {
                unsigned num_bits = (length_slot - 4u) >> 2;
                uint32_t extra = (uint32_t)(bit_buf >> (64u - num_bits));
                XX_RARX50_CONSUME_FAST(num_bits);
                length = 2u + (((size_t)4u | ((size_t)length_slot & 3u)) << num_bits) + (size_t)extra;
            }
            peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
            entry = dist_fast[peek];
            hlen = entry & 0x3Fu;
            if (hlen != 0) {
                distance_slot = entry >> 6;
                XX_RARX50_CONSUME_FAST(hlen);
            } else {
                XX_RARX50_SYNC_BITS();
                status = xx_rarx50_decode_huffman_slow(&tables->distance_table, bits, &distance_slot);
                if (status != XX_RARX_STATUS_OK) goto done;
                XX_RARX50_RESYNC_BITS();
            }
            if (distance_slot < 4u) {
                distance = (size_t)distance_slot + 1u;
            } else {
                unsigned bit_count = (distance_slot - 2u) >> 1;
                size_t base = ((size_t)2u | ((size_t)distance_slot & 1u)) << bit_count;
                uint32_t extra;
                if (align_mode && bit_count >= 4u) {
                    uint32_t high = 0;
                    if (bit_count > 4u) {
                        unsigned high_bits = bit_count - 4u;
                        high = (uint32_t)(bit_buf >> (64u - high_bits));
                        XX_RARX50_CONSUME_FAST(high_bits);
                    }
                    peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
                    entry = align_fast[peek];
                    hlen = entry & 0x3Fu;
                    unsigned low;
                    if (hlen != 0) {
                        low = entry >> 6;
                        XX_RARX50_CONSUME_FAST(hlen);
                    } else {
                        XX_RARX50_SYNC_BITS();
                        status = xx_rarx50_decode_huffman_slow(&tables->align_table, bits, &low);
                        if (status != XX_RARX_STATUS_OK) goto done;
                        XX_RARX50_RESYNC_BITS();
                    }
                    extra = (high << 4) | (uint32_t)low;
                } else {
                    extra = (uint32_t)(bit_buf >> (64u - bit_count));
                    XX_RARX50_CONSUME_FAST(bit_count);
                }
                distance = base + (size_t)extra + 1u;
            }
            bonus = (size_t)(distance > 0x100u) + (size_t)(distance > 0x2000u) + (size_t)(distance > 0x40000u);
            if (length > SIZE_MAX - bonus) {
                status = XX_RARX_STATUS_CORRUPT;
                goto done;
            }
            length += bonus;
            dist3 = dist2;
            dist2 = dist1;
            dist1 = dist0;
            dist0 = distance;
            last_len = length;
            XX_RARX50_PERFORM_MATCH(distance, length);
        }
    }

    bit_pos = anchor_bit_pos + ((size_t)(bit_src - anchor_src) * 8u) + (size_t)(anchor_bits_left - bits_left);

    /* Safe path for end-of-block or end-of-destination */
    while (bit_pos < bit_limit) {
        uint32_t peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
        uint32_t entry = main_fast[peek];
        unsigned hlen = entry & 0x3Fu;
        unsigned symbol;
        if (hlen != 0) {
            symbol = entry >> 6;
            XX_RARX50_CONSUME_SAFE(hlen);
        } else {
            bits->buf = bit_buf;
            bits->bits_left = bits_left;
            bits->bit_position = bit_pos;
            bits->src = bit_src;
            status = xx_rarx50_decode_huffman_slow(&tables->main_table, bits, &symbol);
            if (status != XX_RARX_STATUS_OK) goto done;
            bit_buf = bits->buf;
            bits_left = bits->bits_left;
            bit_pos = bits->bit_position;
            bit_src = bits->src;
        }
        if (symbol < 256u) {
            if (dst >= dst_end) {
                status = XX_RARX_STATUS_CORRUPT;
                goto done;
            }
            *dst++ = (uint8_t)symbol;
            continue;
        } else if (symbol == 256u) {
            bits->buf = bit_buf;
            bits->bits_left = bits_left;
            bits->bit_position = bit_pos;
            bits->src = bit_src;
            status = xx_rarx50_append_filter(lz, (size_t)(dst - destination), bits);
            if (status != XX_RARX_STATUS_OK) goto done;
            bit_buf = bits->buf;
            bits_left = bits->bits_left;
            bit_pos = bits->bit_position;
            bit_src = bits->src;
        } else if (symbol == 257u) {
            if (last_len != 0) {
                XX_RARX50_PERFORM_MATCH(dist0, last_len);
            }
        } else if (symbol <= 261u) {
            size_t distance;
            size_t length;
            unsigned length_slot;
            if (symbol == 258u) {
                distance = dist0;
            } else if (symbol == 259u) {
                distance = dist1;
                dist1 = dist0;
            } else if (symbol == 260u) {
                distance = dist2;
                dist2 = dist1;
                dist1 = dist0;
            } else {
                distance = dist3;
                dist3 = dist2;
                dist2 = dist1;
                dist1 = dist0;
            }
            dist0 = distance;
            if (distance == 0) {
                status = XX_RARX_STATUS_CORRUPT;
                goto done;
            }
            peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
            entry = length_fast[peek];
            hlen = entry & 0x3Fu;
            if (hlen != 0) {
                length_slot = entry >> 6;
                XX_RARX50_CONSUME_SAFE(hlen);
            } else {
                bits->buf = bit_buf;
                bits->bits_left = bits_left;
                bits->bit_position = bit_pos;
                bits->src = bit_src;
                status = xx_rarx50_decode_huffman_slow(&tables->length_table, bits, &length_slot);
                if (status != XX_RARX_STATUS_OK) goto done;
                bit_buf = bits->buf;
                bits_left = bits->bits_left;
                bit_pos = bits->bit_position;
                bit_src = bits->src;
            }
            if (length_slot < 8u) {
                length = (size_t)length_slot + 2u;
            } else {
                unsigned num_bits = (length_slot - 4u) >> 2;
                uint32_t extra = (uint32_t)(bit_buf >> (64u - num_bits));
                XX_RARX50_CONSUME_SAFE(num_bits);
                length = 2u + (((size_t)4u | ((size_t)length_slot & 3u)) << num_bits) + (size_t)extra;
            }
            last_len = length;
            XX_RARX50_PERFORM_MATCH(distance, length);
        } else {
            unsigned length_slot = symbol - 262u;
            unsigned distance_slot;
            size_t distance;
            size_t length;
            size_t bonus;
            if (length_slot < 8u) {
                length = (size_t)length_slot + 2u;
            } else {
                unsigned num_bits = (length_slot - 4u) >> 2;
                uint32_t extra = (uint32_t)(bit_buf >> (64u - num_bits));
                XX_RARX50_CONSUME_SAFE(num_bits);
                length = 2u + (((size_t)4u | ((size_t)length_slot & 3u)) << num_bits) + (size_t)extra;
            }
            peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
            entry = dist_fast[peek];
            hlen = entry & 0x3Fu;
            if (hlen != 0) {
                distance_slot = entry >> 6;
                XX_RARX50_CONSUME_SAFE(hlen);
            } else {
                bits->buf = bit_buf;
                bits->bits_left = bits_left;
                bits->bit_position = bit_pos;
                bits->src = bit_src;
                status = xx_rarx50_decode_huffman_slow(&tables->distance_table, bits, &distance_slot);
                if (status != XX_RARX_STATUS_OK) goto done;
                bit_buf = bits->buf;
                bits_left = bits->bits_left;
                bit_pos = bits->bit_position;
                bit_src = bits->src;
            }
            if (distance_slot < 4u) {
                distance = (size_t)distance_slot + 1u;
            } else {
                unsigned bit_count = (distance_slot - 2u) >> 1;
                size_t base = ((size_t)2u | ((size_t)distance_slot & 1u)) << bit_count;
                uint32_t extra;
                if (align_mode && bit_count >= 4u) {
                    uint32_t high = 0;
                    if (bit_count > 4u) {
                        unsigned high_bits = bit_count - 4u;
                        high = (uint32_t)(bit_buf >> (64u - high_bits));
                        XX_RARX50_CONSUME_SAFE(high_bits);
                    }
                    peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
                    entry = align_fast[peek];
                    hlen = entry & 0x3Fu;
                    unsigned low;
                    if (hlen != 0) {
                        low = entry >> 6;
                        XX_RARX50_CONSUME_SAFE(hlen);
                    } else {
                        bits->buf = bit_buf;
                        bits->bits_left = bits_left;
                        bits->bit_position = bit_pos;
                        bits->src = bit_src;
                        status = xx_rarx50_decode_huffman_slow(&tables->align_table, bits, &low);
                        if (status != XX_RARX_STATUS_OK) goto done;
                        bit_buf = bits->buf;
                        bits_left = bits->bits_left;
                        bit_pos = bits->bit_position;
                        bit_src = bits->src;
                    }
                    extra = (high << 4) | (uint32_t)low;
                } else {
                    extra = (uint32_t)(bit_buf >> (64u - bit_count));
                    XX_RARX50_CONSUME_SAFE(bit_count);
                }
                distance = base + (size_t)extra + 1u;
            }
            bonus = (size_t)(distance > 0x100u) + (size_t)(distance > 0x2000u) + (size_t)(distance > 0x40000u);
            if (length > SIZE_MAX - bonus) {
                status = XX_RARX_STATUS_CORRUPT;
                goto done;
            }
            length += bonus;
            dist3 = dist2;
            dist2 = dist1;
            dist1 = dist0;
            dist0 = distance;
            last_len = length;
            XX_RARX50_PERFORM_MATCH(distance, length);
        }
    }

done:
    bits->buf = bit_buf;
    bits->bits_left = bits_left;
    bits->bit_position = bit_pos;
    bits->src = bit_src;
    lz->position = (size_t)(dst - destination);
    lz->dist0 = dist0;
    lz->dist1 = dist1;
    lz->dist2 = dist2;
    lz->dist3 = dist3;
    lz->last_len = last_len;
    return status;

#undef XX_RARX50_CONSUME_FAST
#undef XX_RARX50_SYNC_BITS
#undef XX_RARX50_RESYNC_BITS
#undef XX_RARX50_CONSUME_SAFE
#undef XX_RARX50_PERFORM_MATCH
}

/*
 * Token path
 * ----------
 * A block is decoded in two steps.  xx_rarx50_tokenize does the Huffman
 * decoding and writes literals and tokens (matches, filter records) to
 * buffers; it needs only the block payload and its tables, not the output.
 * It also follows the repeated distances and the last length through the
 * block, so nearly every match token carries its distance and length; only a
 * match that uses a repeated distance from before the block, when the
 * tokenizer was not given that state (a worker thread), names the slot
 * instead.  xx_rarx50_apply_tokens then replays the tokens in order: it
 * copies literals and matches to the output and registers the filters.
 * Because the tokens are known in advance, the source of every match is
 * prefetched some dozens of tokens before it is copied, which hides most of
 * the cache misses of a large-dictionary stream; and the first step can run
 * on other threads.
 *
 * Neither step reports errors.  Whenever something is not plainly valid they
 * give up, the caller puts the LZ state back to the start of the block and
 * xx_rarx50_decode_block (the reference decoder) decodes the block again, so
 * malformed input always ends with the reference decoder's result.
 */

/* Decodes symbols until the block ends or the room is used up.
 *   tok_room >= XX_RARX50_TOK_MIN_ROOM words,
 *   XX_RARX50_LIT_MIN_ROOM <= lit_room <= XX_RARX50_LIT_MAX_ROOM bytes.
 * Every literal written is counted by a token of the same call.
 * Returns 1 when the block ended exactly at its last bit, 0 when more calls
 * are needed and -1 when the block needs the reference decoder. */
static int xx_rarx50_tokenize(const xx_rarx50_tables *tables, xx_rarx50_bits *bits, xx_rarx50_reps *reps, uint64_t *tok, size_t tok_room, size_t *tok_count, uint8_t *lit,
                              size_t lit_room, size_t *lit_count)
{
    const uint16_t *const main_fast = tables->main_table.fast_table;
    const uint16_t *const length_fast = tables->length_table.fast_table;
    const uint16_t *const dist_fast = tables->distance_table.fast_table;
    const uint16_t *const align_fast = tables->align_table.fast_table;
    const bool align_mode = tables->align_mode;

    /* One pass adds at most two words and three literals; one more word is
     * kept for the literals that follow the last token. */
    uint64_t *tp = tok;
    uint64_t *const tok_stop = tok + (tok_room - 4u);
    uint8_t *lp = lit;
    uint8_t *lit_mark = lit;
    uint8_t *const lit_stop = lit + (lit_room - 4u);

    uint64_t bit_buf = bits->buf;
    int bits_left = bits->bits_left;
    size_t bit_pos = bits->bit_position;
    const size_t bit_limit = bits->bit_limit;
    const uint8_t *bit_src = bits->src;
    const uint8_t *const bit_src_end = bits->src_end;
    uint64_t rep0 = reps->slot[0];
    uint64_t rep1 = reps->slot[1];
    uint64_t rep2 = reps->slot[2];
    uint64_t rep3 = reps->slot[3];
    size_t last_len = reps->last_len;
    bool last_known = reps->last_known;
    int result = 0;

/* A match with a new distance. */
#define XX_RARX50_T_EMIT_NEW()                                                                 \
    do {                                                                                       \
        rep3 = rep2;                                                                           \
        rep2 = rep1;                                                                           \
        rep1 = rep0;                                                                           \
        rep0 = (uint64_t)distance;                                                             \
        last_len = length;                                                                     \
        last_known = true;                                                                     \
        *tp++ = XX_RARX50_TOKEN(lp - lit_mark, XX_RARX50_TOK_MATCH, length, distance - 1u);    \
        lit_mark = lp;                                                                         \
    } while (0)

/* Moves repeated distance (symbol - 258) to the front. */
#define XX_RARX50_T_SELECT_REP()            \
    do {                                    \
        if (symbol == 258u) {               \
            rep_value = rep0;               \
        } else if (symbol == 259u) {        \
            rep_value = rep1;               \
            rep1 = rep0;                    \
        } else if (symbol == 260u) {        \
            rep_value = rep2;               \
            rep2 = rep1;                    \
            rep1 = rep0;                    \
        } else {                            \
            rep_value = rep3;               \
            rep3 = rep2;                    \
            rep2 = rep1;                    \
            rep1 = rep0;                    \
        }                                   \
        rep0 = rep_value;                   \
    } while (0)

/* A match with distance rep_value (known, or one of the block's initial
 * repeated distances) and the given length. */
#define XX_RARX50_T_EMIT_REP(match_length)                                                                                  \
    do {                                                                                                                    \
        if (rep_value >= XX_RARX50_REP_INITIAL) {                                                                           \
            *tp++ = XX_RARX50_TOKEN(lp - lit_mark, XX_RARX50_TOK_INIT0 + (unsigned)(rep_value & 3u), (match_length), 0u);   \
        } else {                                                                                                            \
            if (rep_value == 0) goto bad;                                                                                   \
            *tp++ = XX_RARX50_TOKEN(lp - lit_mark, XX_RARX50_TOK_MATCH, (match_length), rep_value - 1u);                    \
        }                                                                                                                   \
        lit_mark = lp;                                                                                                      \
    } while (0)

/* Symbol 257: the last length with repeated distance 0. */
#define XX_RARX50_T_EMIT_LAST()                                                         \
    do {                                                                                \
        if (!last_known) {                                                              \
            *tp++ = XX_RARX50_TOKEN(lp - lit_mark, XX_RARX50_TOK_LAST, 0u, 0u);         \
            lit_mark = lp;                                                              \
        } else if (last_len != 0) {                                                     \
            const uint64_t rep_value = rep0;                                            \
            XX_RARX50_T_EMIT_REP(last_len);                                             \
        }                                                                               \
    } while (0)

/* Branch-free refill to 56..63 bits; reads 8 bytes at bit_src.  The bits above
 * bits_left are the next bits of the stream, so a later refill ORs the same
 * values over them. */
#define XX_RARX50_T_REFILL()                                   \
    do {                                                       \
        uint64_t _w = xx_mem_load64(bit_src);                  \
        bit_buf |= XX_RARX50_BSWAP64(_w) >> (bit_fill & 63u); \
        bit_src += (size_t)((63u - bit_fill) & 63u) >> 3;     \
        bit_fill |= 56u;                                      \
    } while (0)

#define XX_RARX50_T_DROP(count)            \
    do {                                   \
        unsigned _cnt = (unsigned)(count); \
        bit_buf <<= _cnt;                  \
        bit_fill -= _cnt;                 \
    } while (0)

/* Drops the code of a table entry: its low six bits are the code length. */
#define XX_RARX50_T_DROP_ENTRY()       \
    do {                               \
        bit_buf <<= (entry & 0x3Fu);   \
        bit_fill -= entry;            \
    } while (0)

/* Symbol through the fast table, or the long-code search: the fast region
 * always has more than 15 bits of payload left. */
#define XX_RARX50_T_LONG(table)                                                          \
    do {                                                                                 \
        entry = xx_rarx50_huffman_long(&(table), (uint32_t)(bit_buf >> 49));             \
        if (entry == 0) goto bad;                                                        \
    } while (0)

/* The same steps for the end of the payload, with the reference decoder's
 * 32-bit refill and bit accounting. */
#define XX_RARX50_T_TAKE(count)                                            \
    do {                                                                   \
        unsigned _cnt = (unsigned)(count);                                 \
        bit_buf <<= _cnt;                                                  \
        bits_left -= (int)_cnt;                                            \
        bit_pos += _cnt;                                                   \
        if (bits_left < 32) {                                              \
            if (bit_src + 4 <= bit_src_end) {                              \
                uint32_t _w = xx_mem_load32(bit_src);                      \
                _w = XX_RARX50_BSWAP32(_w);                                \
                bit_buf |= ((uint64_t)_w) << (32 - bits_left);             \
                bit_src += 4;                                              \
                bits_left += 32;                                           \
            } else {                                                       \
                while (bits_left <= 56 && bit_src < bit_src_end) {         \
                    bit_buf |= ((uint64_t)*bit_src++) << (56 - bits_left); \
                    bits_left += 8;                                        \
                }                                                          \
            }                                                              \
        }                                                                  \
    } while (0)

#define XX_RARX50_T_LONG_SAFE(table)                                                          \
    do {                                                                                      \
        entry = xx_rarx50_huffman_long(&(table), (uint32_t)(bit_buf >> 49));                  \
        if (entry == 0 || bit_pos > bit_limit || (entry & 0x3Fu) > bit_limit - bit_pos) goto bad; \
    } while (0)

#define XX_RARX50_T_STORE_BITS()          \
    do {                                  \
        bits->buf = bit_buf;              \
        bits->bits_left = bits_left;      \
        bits->bit_position = bit_pos;     \
        bits->src = bit_src;              \
    } while (0)

#define XX_RARX50_T_LOAD_BITS()           \
    do {                                  \
        bit_buf = bits->buf;              \
        bits_left = bits->bits_left;      \
        bit_pos = bits->bit_position;     \
        bit_src = bits->src;              \
    } while (0)

    /* Fast region: at least 32 payload bytes ahead of the refill pointer.  One
     * pass consumes at most 104 bits, so no load reaches the payload end. */
    if ((size_t)(bit_src_end - bit_src) >= 32u && bits_left >= 0 && bits_left <= 63) {
        const uint8_t *const fast_src_end = bit_src_end - 32;
        bool fast_bits_valid = true;
        const uint8_t *anchor_src = bit_src;
        int anchor_bits_left = bits_left;
        size_t anchor_bit_pos = bit_pos;
        /* The bit count of the fast region: only its low six bits count (the
         * true count stays within 0..63 here), so a table entry is subtracted
         * as it is, symbol bits included. */
        uint32_t bit_fill = (uint32_t)bits_left;

        while (bit_src <= fast_src_end && tp <= tok_stop && lp <= lit_stop) {
            uint32_t entry;
            unsigned symbol;

            XX_RARX50_T_REFILL();
            entry = main_fast[bit_buf >> 52];
            /* A literal with a short code: nonzero entry below 256 << 6. */
            if ((uint32_t)(entry - 1u) < 0x3fffu) {
                *lp++ = (uint8_t)(entry >> 6);
                XX_RARX50_T_DROP_ENTRY();
                entry = main_fast[bit_buf >> 52];
                if ((uint32_t)(entry - 1u) < 0x3fffu) {
                    *lp++ = (uint8_t)(entry >> 6);
                    XX_RARX50_T_DROP_ENTRY();
                    entry = main_fast[bit_buf >> 52];
                    if ((uint32_t)(entry - 1u) < 0x3fffu) {
                        *lp++ = (uint8_t)(entry >> 6);
                        XX_RARX50_T_DROP_ENTRY();
                        continue;
                    }
                }
                XX_RARX50_T_REFILL();
            }
            if (entry == 0) {
                XX_RARX50_T_LONG(tables->main_table);
            }
            symbol = entry >> 6;
            XX_RARX50_T_DROP_ENTRY();
            if (symbol < 256u) {
                *lp++ = (uint8_t)symbol;
                continue;
            }
            if (symbol >= 262u) {
                unsigned length_slot = symbol - 262u;
                unsigned distance_slot;
                size_t distance;
                size_t length;
                if (length_slot < 8u) {
                    length = (size_t)length_slot + 2u;
                } else {
                    unsigned num_bits = (length_slot - 4u) >> 2;
                    uint32_t extra = (uint32_t)(bit_buf >> (64u - num_bits));
                    XX_RARX50_T_DROP(num_bits);
                    length = 2u + (((size_t)4u | ((size_t)length_slot & 3u)) << num_bits) + (size_t)extra;
                }
                entry = dist_fast[bit_buf >> 52];
                if (entry == 0) {
                    XX_RARX50_T_LONG(tables->distance_table);
                }
                distance_slot = entry >> 6;
                XX_RARX50_T_DROP_ENTRY();
                if (distance_slot < 4u) {
                    distance = (size_t)distance_slot + 1u;
                } else {
                    unsigned bit_count = (distance_slot - 2u) >> 1;
                    size_t base = ((size_t)2u | ((size_t)distance_slot & 1u)) << bit_count;
                    uint32_t extra;
                    XX_RARX50_T_REFILL();
                    if (align_mode && bit_count >= 4u) {
                        uint32_t high = 0;
                        if (bit_count > 4u) {
                            unsigned high_bits = bit_count - 4u;
                            high = (uint32_t)(bit_buf >> (64u - high_bits));
                            XX_RARX50_T_DROP(high_bits);
                        }
                        entry = align_fast[bit_buf >> 52];
                        if (entry == 0) {
                            XX_RARX50_T_LONG(tables->align_table);
                        }
                        extra = (high << 4) | (entry >> 6);
                        XX_RARX50_T_DROP_ENTRY();
                    } else {
                        extra = (uint32_t)(bit_buf >> (64u - bit_count));
                        XX_RARX50_T_DROP(bit_count);
                    }
                    distance = base + (size_t)extra + 1u;
                }
                length += (size_t)(distance > 0x100u) + (size_t)(distance > 0x2000u) + (size_t)(distance > 0x40000u);
                XX_RARX50_T_EMIT_NEW();
            } else if (symbol >= 258u) {
                unsigned length_slot;
                size_t length;
                uint64_t rep_value;
                XX_RARX50_T_SELECT_REP();
                entry = length_fast[bit_buf >> 52];
                if (entry == 0) {
                    XX_RARX50_T_LONG(tables->length_table);
                }
                length_slot = entry >> 6;
                XX_RARX50_T_DROP_ENTRY();
                if (length_slot < 8u) {
                    length = (size_t)length_slot + 2u;
                } else {
                    unsigned num_bits = (length_slot - 4u) >> 2;
                    uint32_t extra = (uint32_t)(bit_buf >> (64u - num_bits));
                    XX_RARX50_T_DROP(num_bits);
                    length = 2u + (((size_t)4u | ((size_t)length_slot & 3u)) << num_bits) + (size_t)extra;
                }
                last_len = length;
                last_known = true;
                XX_RARX50_T_EMIT_REP(length);
            } else if (symbol == 257u) {
                XX_RARX50_T_EMIT_LAST();
            } else {
                /* Filter record: read with the checked bit reader. */
                bits_left = (int)(bit_fill & 63u);
                bit_buf &= ~(UINT64_MAX >> bits_left);
                bit_pos = anchor_bit_pos + ((size_t)(bit_src - anchor_src) * 8u) + (size_t)(anchor_bits_left - bits_left);
                XX_RARX50_T_STORE_BITS();
                if (!xx_rarx50_parse_filter(bits, (unsigned)(lp - lit_mark), tp)) goto bad;
                tp += 2;
                lit_mark = lp;
                XX_RARX50_T_LOAD_BITS();
                anchor_src = bit_src;
                anchor_bits_left = bits_left;
                anchor_bit_pos = bit_pos;
                if (bits_left < 0 || bits_left > 63) {
                    fast_bits_valid = false;
                    break;
                }
                bit_fill = (uint32_t)bits_left;
            }
        }
        if (fast_bits_valid) bits_left = (int)(bit_fill & 63u);

        /* Back to the reference form: no stray bits above bits_left. */
        if (fast_bits_valid && (size_t)(bit_src_end - bit_src) >= 8u) {
            bit_fill = (uint32_t)bits_left;
            XX_RARX50_T_REFILL();
            bits_left = (int)(bit_fill & 63u);
            bit_buf &= ~(UINT64_MAX >> bits_left);
        }
        bit_pos = anchor_bit_pos + ((size_t)(bit_src - anchor_src) * 8u) + (size_t)(anchor_bits_left - bits_left);
    }

    /* End of the payload, and of the output room. */
    while (bit_pos < bit_limit) {
        uint32_t entry;
        unsigned symbol;

        if (tp > tok_stop || lp > lit_stop) goto finish;
        entry = main_fast[bit_buf >> 52];
        if (entry == 0) {
            XX_RARX50_T_LONG_SAFE(tables->main_table);
        }
        symbol = entry >> 6;
        XX_RARX50_T_TAKE(entry & 0x3Fu);
        if (symbol < 256u) {
            *lp++ = (uint8_t)symbol;
            continue;
        }
        if (symbol >= 262u) {
            unsigned length_slot = symbol - 262u;
            unsigned distance_slot;
            size_t distance;
            size_t length;
            if (length_slot < 8u) {
                length = (size_t)length_slot + 2u;
            } else {
                unsigned num_bits = (length_slot - 4u) >> 2;
                uint32_t extra = (uint32_t)(bit_buf >> (64u - num_bits));
                XX_RARX50_T_TAKE(num_bits);
                length = 2u + (((size_t)4u | ((size_t)length_slot & 3u)) << num_bits) + (size_t)extra;
            }
            entry = dist_fast[bit_buf >> 52];
            if (entry == 0) {
                XX_RARX50_T_LONG_SAFE(tables->distance_table);
            }
            distance_slot = entry >> 6;
            XX_RARX50_T_TAKE(entry & 0x3Fu);
            if (distance_slot < 4u) {
                distance = (size_t)distance_slot + 1u;
            } else {
                unsigned bit_count = (distance_slot - 2u) >> 1;
                size_t base = ((size_t)2u | ((size_t)distance_slot & 1u)) << bit_count;
                uint32_t extra;
                if (align_mode && bit_count >= 4u) {
                    uint32_t high = 0;
                    if (bit_count > 4u) {
                        unsigned high_bits = bit_count - 4u;
                        high = (uint32_t)(bit_buf >> (64u - high_bits));
                        XX_RARX50_T_TAKE(high_bits);
                    }
                    entry = align_fast[bit_buf >> 52];
                    if (entry == 0) {
                        XX_RARX50_T_LONG_SAFE(tables->align_table);
                    }
                    extra = (high << 4) | (entry >> 6);
                    XX_RARX50_T_TAKE(entry & 0x3Fu);
                } else {
                    extra = (uint32_t)(bit_buf >> (64u - bit_count));
                    XX_RARX50_T_TAKE(bit_count);
                }
                distance = base + (size_t)extra + 1u;
            }
            length += (size_t)(distance > 0x100u) + (size_t)(distance > 0x2000u) + (size_t)(distance > 0x40000u);
            XX_RARX50_T_EMIT_NEW();
        } else if (symbol >= 258u) {
            unsigned length_slot;
            size_t length;
            uint64_t rep_value;
            XX_RARX50_T_SELECT_REP();
            entry = length_fast[bit_buf >> 52];
            if (entry == 0) {
                XX_RARX50_T_LONG_SAFE(tables->length_table);
            }
            length_slot = entry >> 6;
            XX_RARX50_T_TAKE(entry & 0x3Fu);
            if (length_slot < 8u) {
                length = (size_t)length_slot + 2u;
            } else {
                unsigned num_bits = (length_slot - 4u) >> 2;
                uint32_t extra = (uint32_t)(bit_buf >> (64u - num_bits));
                XX_RARX50_T_TAKE(num_bits);
                length = 2u + (((size_t)4u | ((size_t)length_slot & 3u)) << num_bits) + (size_t)extra;
            }
            last_len = length;
            last_known = true;
            XX_RARX50_T_EMIT_REP(length);
        } else if (symbol == 257u) {
            XX_RARX50_T_EMIT_LAST();
        } else {
            XX_RARX50_T_STORE_BITS();
            if (!xx_rarx50_parse_filter(bits, (unsigned)(lp - lit_mark), tp)) goto bad;
            tp += 2;
            lit_mark = lp;
            XX_RARX50_T_LOAD_BITS();
        }
    }
    if (bit_pos != bit_limit) goto bad;
    result = 1;

finish:
    if (lp != lit_mark) {
        *tp++ = XX_RARX50_TOKEN(lp - lit_mark, XX_RARX50_TOK_LITS, 0u, 0u);
    }
    XX_RARX50_T_STORE_BITS();
    reps->slot[0] = rep0;
    reps->slot[1] = rep1;
    reps->slot[2] = rep2;
    reps->slot[3] = rep3;
    reps->last_len = last_len;
    reps->last_known = last_known;
    *tok_count = (size_t)(tp - tok);
    *lit_count = (size_t)(lp - lit);
    return result;

bad:
    return -1;

#undef XX_RARX50_T_EMIT_NEW
#undef XX_RARX50_T_SELECT_REP
#undef XX_RARX50_T_EMIT_REP
#undef XX_RARX50_T_EMIT_LAST
#undef XX_RARX50_T_REFILL
#undef XX_RARX50_T_DROP
#undef XX_RARX50_T_DROP_ENTRY
#undef XX_RARX50_T_LONG
#undef XX_RARX50_T_TAKE
#undef XX_RARX50_T_LONG_SAFE
#undef XX_RARX50_T_STORE_BITS
#undef XX_RARX50_T_LOAD_BITS
}

/* One resolved token: literals to copy, then a match or a filter. */
#define XX_RARX50_OP_NONE 0u   /* literals only */
#define XX_RARX50_OP_FAST 1u   /* match that needs no checks: 16 <= distance <= output position, and the
                                  literals and the match end XX_RARX50_OP_SLACK bytes before the output end */
#define XX_RARX50_OP_MATCH 2u  /* any other match */
#define XX_RARX50_OP_FILTER 3u /* filter record */
#define XX_RARX50_OP_SLACK 64u
#define XX_RARX50_POSITION_HEADROOM 0x20000u /* more than the literals of one token */

typedef struct xx_rarx50_op_s {
    size_t value; /* match distance, or the index of the filter's token */
    uint32_t literals;
    uint16_t length;
    uint16_t kind;
} xx_rarx50_op;

typedef struct xx_rarx50_resolver_s {
    const uint64_t *tok_base; /* first token of the call */
    const uint64_t *tok;      /* next token */
    const uint64_t *tok_end;
    const uint8_t *destination;
    size_t destination_size;
    size_t position;      /* output position after the tokens resolved so far */
    size_t literals_left; /* literals not yet claimed by a token */
    size_t initial_distance[4]; /* LZ state at the start of the block */
    size_t initial_last_len;
} xx_rarx50_resolver;

/* Resolves up to XX_RARX50_APPLY_CHUNK tokens: every bound that does not need
 * the output bytes is checked, the few tokens that refer to the LZ state at
 * the start of the block get their values, and the sources of the matches
 * are prefetched.  Returns the number of operations, or SIZE_MAX to give up.
 * destination_size is at most SIZE_MAX - XX_RARX50_POSITION_HEADROOM, so the
 * position cannot wrap before it is compared. */
static size_t xx_rarx50_resolve(xx_rarx50_resolver *resolver, xx_rarx50_op *ops)
{
    const uint64_t *tp = resolver->tok;
    const uint64_t *const tok_end = resolver->tok_end;
    const uint8_t *const destination = resolver->destination;
    const size_t destination_size = resolver->destination_size;
    size_t position = resolver->position;
    size_t literal_total = 0;
    xx_rarx50_op *op = ops;
    xx_rarx50_op *const op_end = ops + XX_RARX50_APPLY_CHUNK;

    for (; op != op_end && tp != tok_end; ++op) {
        const uint64_t token = *tp++;
        const unsigned kind = (unsigned)(token >> 45) & 7u;
        const size_t literals = (size_t)(token >> 48);
        size_t length = (size_t)((token >> 32) & 0x1fffu);
        size_t distance = (size_t)(uint32_t)token + 1u;
        size_t source;
        size_t source_last;

        literal_total += literals;
        position += literals;
        op->literals = (uint32_t)literals;
        if (kind != XX_RARX50_TOK_MATCH) {
            bool nothing = false;
            if (kind <= XX_RARX50_TOK_INIT3) {
                distance = resolver->initial_distance[kind - XX_RARX50_TOK_INIT0];
                if (distance == 0) return SIZE_MAX;
            } else if (kind == XX_RARX50_TOK_LAST) {
                length = resolver->initial_last_len;
                distance = resolver->initial_distance[0];
                if (length > 0x1fffu) return SIZE_MAX;
                nothing = length == 0;
            } else if (kind == XX_RARX50_TOK_FILTER) {
                if (tp == tok_end || position > destination_size) return SIZE_MAX;
                op->kind = XX_RARX50_OP_FILTER;
                op->value = (size_t)(tp - 1 - resolver->tok_base);
                tp++;
                continue;
            } else {
                nothing = true;
            }
            if (nothing) {
                if (position > destination_size) return SIZE_MAX;
                op->kind = XX_RARX50_OP_NONE;
                continue;
            }
        }
        if (position > destination_size || length > destination_size - position) return SIZE_MAX;
        op->value = distance;
        op->length = (uint16_t)length;
        op->kind = (uint16_t)(XX_RARX50_OP_MATCH -
                              (unsigned)((distance >= 16u) & (distance <= position) & (destination_size - position - length >= XX_RARX50_OP_SLACK)));
        /* The copy reads up to 32 bytes at the source, which may span two cache
         * lines.  Without a valid source the output start is touched instead. */
        source = distance <= position ? position - distance : 0;
        source_last = source + 31u < destination_size ? source + 31u : source;
        XX_RARX50_PREFETCH(destination + source);
        XX_RARX50_PREFETCH(destination + source_last);
        position += length;
    }
    if (literal_total > resolver->literals_left) return SIZE_MAX;

    resolver->tok = tp;
    resolver->position = position;
    resolver->literals_left -= literal_total;
    return (size_t)(op - ops);
}

#if defined(XX_RARX50_USE_SSE2)
#define XX_RARX50_COPY16(to, from) _mm_storeu_si128((__m128i *)(void *)(to), _mm_loadu_si128((const __m128i *)(const void *)(from)))
#else
#define XX_RARX50_COPY16(to, from)      \
    do {                                \
        memcpy((to), (from), 8);        \
        memcpy((to) + 8, (from) + 8, 8); \
    } while (0)
#endif

/* Replays the tokens of one xx_rarx50_tokenize call (or of several calls
 * whose buffers are contiguous) on the output.  The literal buffer must be
 * readable for XX_RARX50_LIT_SLACK bytes beyond lit_count.  false: nothing
 * can be relied on; the caller restores *lz and uses the reference decoder. */
static bool xx_rarx50_apply_tokens(const xx_rarx50_state *state, xx_rarx50_lz *lz, size_t window_size, const uint64_t *tok, size_t tok_count, const uint8_t *lit,
                                   size_t lit_count)
{
    xx_rarx50_op ops[2][XX_RARX50_APPLY_CHUNK];
    size_t counts[2];
    unsigned current = 0;
    xx_rarx50_resolver resolver;
    uint8_t *const destination = lz->destination;
    const size_t destination_size = lz->destination_size;
    uint8_t *dst = destination + lz->position;
    const uint8_t *const dst_end = destination + destination_size;
    const uint8_t *lp = lit;

    if (lz->position > destination_size || destination_size > SIZE_MAX - XX_RARX50_POSITION_HEADROOM) return false;
    resolver.tok_base = tok;
    resolver.tok = tok;
    resolver.tok_end = tok + tok_count;
    resolver.destination = destination;
    resolver.destination_size = destination_size;
    resolver.position = lz->position;
    resolver.literals_left = lit_count;
    resolver.initial_distance[0] = lz->dist0;
    resolver.initial_distance[1] = lz->dist1;
    resolver.initial_distance[2] = lz->dist2;
    resolver.initial_distance[3] = lz->dist3;
    resolver.initial_last_len = lz->last_len;

    counts[0] = xx_rarx50_resolve(&resolver, ops[0]);
    if (counts[0] == SIZE_MAX) return false;
    while (counts[current] != 0) {
        const xx_rarx50_op *op = ops[current];
        const xx_rarx50_op *const op_end = op + counts[current];

        /* Resolve (and prefetch for) the next chunk before copying this one. */
        counts[current ^ 1u] = xx_rarx50_resolve(&resolver, ops[current ^ 1u]);
        if (counts[current ^ 1u] == SIZE_MAX) return false;

        for (; op != op_end; ++op) {
            const size_t literals = op->literals;
            if (op->kind == XX_RARX50_OP_FAST) {
                /* The resolver checked every bound; the moves may write up to
                 * 32 bytes beyond the data, inside XX_RARX50_OP_SLACK. */
                const uint8_t *src;
                const size_t length = op->length;
                XX_RARX50_COPY16(dst, lp);
                if (literals > 16u) {
                    size_t done = 16u;
                    do {
                        XX_RARX50_COPY16(dst + done, lp + done);
                        done += 16u;
                    } while (done < literals);
                }
                dst += literals;
                lp += literals;
                src = dst - op->value;
                XX_RARX50_COPY16(dst, src);
                XX_RARX50_COPY16(dst + 16, src + 16);
                if (length > 32u) {
                    size_t done = 32u;
                    do {
                        XX_RARX50_COPY16(dst + done, src + done);
                        done += 16u;
                    } while (done < length);
                }
                dst += length;
                continue;
            }
            if (literals != 0) {
                memcpy(dst, lp, literals);
                dst += literals;
                lp += literals;
            }
            if (op->kind == XX_RARX50_OP_MATCH) {
                const size_t distance = op->value;
                const size_t length = op->length;
                if (length <= (size_t)(dst_end - dst) && distance != 0 && distance <= (size_t)(dst - destination)) {
                    xx_rarx50_copy_match_fast_ptr(dst, distance, length, dst_end);
                    dst += length;
                } else {
                    size_t current_pos = (size_t)(dst - destination);
                    if (xx_rarx50_copy_match(state, destination, &current_pos, destination_size, window_size, distance, length) != XX_RARX_STATUS_OK) return false;
                    dst = destination + current_pos;
                }
            } else if (op->kind == XX_RARX50_OP_FILTER) {
                const uint64_t record = tok[op->value];
                if (!xx_rarx50_add_filter(lz, (size_t)(dst - destination), (uint32_t)record, (uint32_t)tok[op->value + 1u], (uint32_t)(record >> 32) & 0x1fffu)) return false;
            }
        }
        current ^= 1u;
    }
    if (resolver.literals_left != 0 || (size_t)(dst - destination) != resolver.position) return false;

    lz->position = resolver.position;
    return true;
}

/* The tokenizer starts a block knowing the LZ state (same thread).  false:
 * the state cannot be expressed in tokens; use the reference decoder. */
static bool xx_rarx50_reps_begin_known(xx_rarx50_reps *reps, const xx_rarx50_lz *lz)
{
    if ((uint64_t)lz->dist0 >= XX_RARX50_REP_INITIAL || (uint64_t)lz->dist1 >= XX_RARX50_REP_INITIAL || (uint64_t)lz->dist2 >= XX_RARX50_REP_INITIAL ||
        (uint64_t)lz->dist3 >= XX_RARX50_REP_INITIAL || lz->last_len > 0x1fffu) {
        return false;
    }
    reps->slot[0] = (uint64_t)lz->dist0;
    reps->slot[1] = (uint64_t)lz->dist1;
    reps->slot[2] = (uint64_t)lz->dist2;
    reps->slot[3] = (uint64_t)lz->dist3;
    reps->last_len = lz->last_len;
    reps->last_known = true;
    return true;
}

#if XX_RARX50_MT
/* The tokenizer starts a block without the LZ state (worker thread). */
static void xx_rarx50_reps_begin_unknown(xx_rarx50_reps *reps)
{
    reps->slot[0] = XX_RARX50_REP_INITIAL;
    reps->slot[1] = XX_RARX50_REP_INITIAL + 1u;
    reps->slot[2] = XX_RARX50_REP_INITIAL + 2u;
    reps->slot[3] = XX_RARX50_REP_INITIAL + 3u;
    reps->last_len = 0;
    reps->last_known = false;
}
#endif

/* After every token of the block was applied: the LZ state at the block end. */
static void xx_rarx50_reps_finish(const xx_rarx50_reps *reps, xx_rarx50_lz *lz)
{
    size_t initial[4];
    size_t final[4];
    unsigned i;
    initial[0] = lz->dist0;
    initial[1] = lz->dist1;
    initial[2] = lz->dist2;
    initial[3] = lz->dist3;
    for (i = 0; i < 4u; ++i) {
        final[i] = reps->slot[i] >= XX_RARX50_REP_INITIAL ? initial[reps->slot[i] & 3u] : (size_t)reps->slot[i];
    }
    lz->dist0 = final[0];
    lz->dist1 = final[1];
    lz->dist2 = final[2];
    lz->dist3 = final[3];
    if (reps->last_known) lz->last_len = reps->last_len;
}

/* Puts the LZ state back to `saved` after a token-path attempt.  The filter
 * array is kept (it may have grown); only its count goes back. */
static void xx_rarx50_lz_rollback(xx_rarx50_lz *lz, const xx_rarx50_lz *saved)
{
    lz->position = saved->position;
    lz->dist0 = saved->dist0;
    lz->dist1 = saved->dist1;
    lz->dist2 = saved->dist2;
    lz->dist3 = saved->dist3;
    lz->last_len = saved->last_len;
    lz->filter_count = saved->filter_count;
}

/* Token-path decoding of one block on the calling thread.  false: the caller
 * rolls back and uses the reference decoder. */
static bool xx_rarx50_decode_block_tokens(const xx_rarx50_state *state, const xx_rarx50_tables *tables, xx_rarx50_lz *lz, size_t window_size, xx_rarx50_bits *bits)
{
    xx_rarx50_reps reps;

    if (!xx_rarx50_reps_begin_known(&reps, lz)) return false;
    for (;;) {
        size_t tok_count = 0;
        size_t lit_count = 0;
        int result = xx_rarx50_tokenize(tables, bits, &reps, state->token_buffer, XX_RARX50_ST_TOKENS, &tok_count, state->literal_buffer, XX_RARX50_ST_LITERALS,
                                        &lit_count);
        if (result < 0) return false;
        if (result == 0 && tok_count == 0) return false;
        if (!xx_rarx50_apply_tokens(state, lz, window_size, state->token_buffer, tok_count, state->literal_buffer, lit_count)) return false;
        if (result > 0) break;
    }
    xx_rarx50_reps_finish(&reps, lz);
    return true;
}

typedef struct xx_rarx50_block_header_s {
    uint8_t flags;
    size_t header_size;
    size_t payload_size;
    size_t payload_bits;
} xx_rarx50_block_header;

static xx_rarx_status_t xx_rarx50_read_block_header(const uint8_t *source, size_t source_size, size_t source_position, xx_rarx50_block_header *header)
{
    uint8_t flags;
    uint8_t checksum;
    unsigned size_width_code;
    size_t size_width;
    size_t header_size;
    size_t payload_size = 0;
    size_t i;

    if (source_position > source_size || source_size - source_position < 3u) {
        return XX_RARX_STATUS_TRUNCATED;
    }
    flags = source[source_position];
    checksum = source[source_position + 1u];
    size_width_code = (unsigned)((flags >> 3) & 3u);
    if (size_width_code == 3u) {
        return XX_RARX_STATUS_CORRUPT;
    }
    size_width = (size_t)size_width_code + 1u;
    header_size = 2u + size_width;
    if (header_size > source_size - source_position) {
        return XX_RARX_STATUS_TRUNCATED;
    }
    for (i = 0; i < size_width; ++i) {
        uint8_t byte_value = source[source_position + 2u + i];
        checksum ^= byte_value;
        payload_size |= (size_t)byte_value << (i * 8u);
    }
    checksum ^= flags;
    if (checksum != 0x5au) {
        return XX_RARX_STATUS_CORRUPT;
    }
    if (payload_size > source_size - source_position - header_size) {
        return XX_RARX_STATUS_TRUNCATED;
    }
    header->flags = flags;
    header->header_size = header_size;
    header->payload_size = payload_size;
    if (payload_size == 0) {
        header->payload_bits = 0;
    } else {
        unsigned final_bits = (unsigned)(flags & 7u) + 1u;
        header->payload_bits = (payload_size - 1u) * 8u + final_bits;
    }
    return XX_RARX_STATUS_OK;
}

static void xx_rarx50_stream_output(xx_rarx50_state *state, const xx_rarx50_lz *lz)
{
    if (state->chunk_cb && lz->position > state->stream_pos) {
        state->chunk_cb(state->chunk_user_data, lz->destination + state->stream_pos, lz->position - state->stream_pos);
        state->stream_pos = lz->position;
    }
}

#if XX_RARX50_MT
/*
 * Parallel decoding
 * -----------------
 * The block headers give every block boundary, and a block that carries
 * tables (flag 0x80) is self-contained up to the LZ state, so worker threads
 * tokenize the blocks while the calling thread walks the headers, applies the
 * finished blocks in stream order and, when it would otherwise wait, tokenizes
 * blocks itself.  A block without tables names the block whose tables it
 * uses; a worker that has not just built those tables reads them again.
 *
 * At most slot_count blocks are in flight.  The run ends at the last block, at
 * the first block that is not plainly valid or does not fit the per-block
 * limits, or on cancellation; the sequential loop of xx_rarx50_decode takes
 * over from the first block that was not applied, so its results (including
 * every error) are those of the sequential decoder.  All threads are joined
 * before xx_rarx50_decode returns.
 */
#define XX_RARX50_MT_MAX_THREADS 16u /* the calling thread and up to 15 workers */
#define XX_RARX50_MT_SLOTS_PER_THREAD 4u
/* Remaining packed bytes worth starting a parallel run for. */
#ifndef XX_RARX50_MT_MIN_SOURCE
#define XX_RARX50_MT_MIN_SOURCE ((size_t)1u << 22)
#endif
/* Packed bytes per worker thread below which fewer threads are started. */
#ifndef XX_RARX50_MT_SOURCE_PER_THREAD
#define XX_RARX50_MT_SOURCE_PER_THREAD ((size_t)1u << 18)
#endif
/* Blocks with a larger payload, or with more tokens or literals than the
 * limits below, are decoded sequentially. */
#ifndef XX_RARX50_MT_MAX_PAYLOAD
#define XX_RARX50_MT_MAX_PAYLOAD ((size_t)1u << 20)
#endif
#define XX_RARX50_MT_MAX_RUNS 8u                    /* parallel runs per member */
#define XX_RARX50_MT_TOK_INITIAL ((size_t)1u << 13) /* words */
#ifndef XX_RARX50_MT_TOK_LIMIT
#define XX_RARX50_MT_TOK_LIMIT ((size_t)1u << 18)
#endif
#define XX_RARX50_MT_LIT_INITIAL ((size_t)1u << 15) /* bytes */
#ifndef XX_RARX50_MT_LIT_LIMIT
#define XX_RARX50_MT_LIT_LIMIT ((size_t)1u << 20)
#endif
#define XX_RARX50_MT_TOK_ROOM 1024u                     /* room wanted before a tokenizer call */
#define XX_RARX50_MT_LIT_ROOM 4096u
#define XX_RARX50_MT_FILTER_BATCH 8u
/* Fewer filters are applied by the calling thread alone. */
#ifndef XX_RARX50_MT_MIN_FILTERS
#define XX_RARX50_MT_MIN_FILTERS 64u
#endif

typedef struct xx_rarx50_mt_slot_s {
    /* Written by the calling thread before the block is posted. */
    const uint8_t *payload;
    size_t payload_size;
    size_t payload_bits;
    const uint8_t *table_payload; /* the block whose tables this block uses (itself with flag 0x80) */
    size_t table_payload_size;
    size_t table_payload_bits;
    size_t source_end;
    uint8_t flags;
    /* Written by the thread that tokenizes the block. */
    uint64_t *tok;
    size_t tok_capacity;
    size_t tok_count;
    uint8_t *lit;
    size_t lit_capacity; /* without XX_RARX50_LIT_SLACK */
    size_t lit_count;
    xx_rarx50_reps reps; /* state at the block end, relative to its start */
    size_t tok_high;     /* most words / bytes any block left in the buffers */
    size_t lit_high;
    /* Guarded by the mutex. */
    bool ok;
    bool done;
} xx_rarx50_mt_slot;

struct xx_rarx50_mt_s;

typedef struct xx_rarx50_mt_worker_s {
    struct xx_rarx50_mt_s *mt;
    unsigned index;
    xx_thread_t *thread;
    xx_rarx50_tables *tables;
    const uint8_t *table_payload; /* block whose tables are in `tables`, or NULL */
} xx_rarx50_mt_worker;

typedef struct xx_rarx50_mt_s {
    xx_mutex_t *mutex;
    xx_cond_t *work_cond; /* workers: a block or a filter batch may be available, or stop */
    xx_cond_t *done_cond; /* calling thread: a worker finished something */
    xx_rarx50_mt_worker workers[XX_RARX50_MT_MAX_THREADS];
    unsigned worker_count;
    xx_rarx50_mt_worker self; /* the calling thread, when it tokenizes */
    xx_rarx50_mt_slot *slots;
    size_t slot_count;
    /* Guarded by the mutex.  Block numbers run on across the runs of a member;
     * block n uses slot n % slot_count. */
    size_t posted;
    size_t claimed;
    unsigned active; /* workers busy outside the mutex */
    bool stop;
    /* Filter job (guarded by the mutex, the arrays are read-only meanwhile). */
    uint8_t *filter_destination;
    size_t filter_destination_size;
    const xx_rarx50_filter *filter_items;
    size_t filter_count;
    size_t filter_next;
    size_t filter_error_index;
    xx_rarx_status_t filter_error;
    /* Set to make busy threads return early. */
    volatile long abort_flag;
} xx_rarx50_mt;

static bool xx_rarx50_mt_grow_tokens(xx_rarx50_mt_slot *slot)
{
    size_t new_capacity = slot->tok_capacity == 0 ? XX_RARX50_MT_TOK_INITIAL : slot->tok_capacity * 2u;
    uint64_t *resized;
    if (slot->tok_capacity >= XX_RARX50_MT_TOK_LIMIT) return false;
    if (slot->tok_count > slot->tok_high) slot->tok_high = slot->tok_count;
    resized = (uint64_t *)xx_mem_alloc(new_capacity * sizeof(uint64_t));
    if (!resized) return false;
    if (slot->tok) {
        memcpy(resized, slot->tok, slot->tok_count * sizeof(uint64_t));
        xx_rarx_clear_free(slot->tok, slot->tok_high * sizeof(uint64_t));
    }
    slot->tok = resized;
    slot->tok_capacity = new_capacity;
    slot->tok_high = slot->tok_count;
    return true;
}

static bool xx_rarx50_mt_grow_literals(xx_rarx50_mt_slot *slot)
{
    size_t new_capacity = slot->lit_capacity == 0 ? XX_RARX50_MT_LIT_INITIAL : slot->lit_capacity * 2u;
    uint8_t *resized;
    if (slot->lit_capacity >= XX_RARX50_MT_LIT_LIMIT) return false;
    if (slot->lit_count > slot->lit_high) slot->lit_high = slot->lit_count;
    resized = (uint8_t *)xx_mem_calloc(new_capacity + XX_RARX50_LIT_SLACK, 1);
    if (!resized) return false;
    if (slot->lit) {
        memcpy(resized, slot->lit, slot->lit_count);
        xx_rarx_clear_free(slot->lit, slot->lit_high);
    }
    slot->lit = resized;
    slot->lit_capacity = new_capacity;
    slot->lit_high = slot->lit_count;
    return true;
}

/* Tokenizes one block into its slot.  false: the block is left to the
 * sequential decoder. */
static bool xx_rarx50_mt_tokenize_payload(xx_rarx50_mt_worker *worker, xx_rarx50_mt_slot *slot)
{
    xx_rarx50_mt *const mt = worker->mt;
    xx_rarx50_bits bits;

    slot->tok_count = 0;
    slot->lit_count = 0;
    xx_rarx50_reps_begin_unknown(&slot->reps);
    if ((slot->flags & 0x80u) != 0) {
        worker->table_payload = NULL;
        xx_rarx50_bits_init(&bits, slot->payload, slot->payload_size, slot->payload_bits);
        if (xx_rarx50_read_tables(worker->tables, &bits) != XX_RARX_STATUS_OK) return false;
        worker->table_payload = slot->payload;
    } else {
        if (worker->table_payload != slot->table_payload) {
            worker->table_payload = NULL;
            xx_rarx50_bits_init(&bits, slot->table_payload, slot->table_payload_size, slot->table_payload_bits);
            if (xx_rarx50_read_tables(worker->tables, &bits) != XX_RARX_STATUS_OK) return false;
            worker->table_payload = slot->table_payload;
        }
        xx_rarx50_bits_init(&bits, slot->payload, slot->payload_size, slot->payload_bits);
    }
    for (;;) {
        size_t tok_room;
        size_t lit_room;
        size_t tok_count = 0;
        size_t lit_count = 0;
        int result;

        if (xx_atomic_load(&mt->abort_flag) != 0) return false;
        if (slot->tok_capacity - slot->tok_count < XX_RARX50_MT_TOK_ROOM && !xx_rarx50_mt_grow_tokens(slot)) return false;
        if (slot->lit_capacity - slot->lit_count < XX_RARX50_MT_LIT_ROOM && !xx_rarx50_mt_grow_literals(slot)) return false;
        tok_room = slot->tok_capacity - slot->tok_count;
        lit_room = slot->lit_capacity - slot->lit_count;
        if (lit_room > XX_RARX50_LIT_MAX_ROOM) lit_room = XX_RARX50_LIT_MAX_ROOM;
        result = xx_rarx50_tokenize(worker->tables, &bits, &slot->reps, slot->tok + slot->tok_count, tok_room, &tok_count, slot->lit + slot->lit_count, lit_room,
                                    &lit_count);
        if (result < 0) return false;
        if (result == 0 && tok_count == 0) return false;
        slot->tok_count += tok_count;
        slot->lit_count += lit_count;
        if (result > 0) return true;
    }
}

static bool xx_rarx50_mt_tokenize_block(xx_rarx50_mt_worker *worker, xx_rarx50_mt_slot *slot)
{
    const bool ok = xx_rarx50_mt_tokenize_payload(worker, slot);
    if (slot->tok_count > slot->tok_high) slot->tok_high = slot->tok_count;
    if (slot->lit_count > slot->lit_high) slot->lit_high = slot->lit_count;
    return ok;
}

/* Applies filters [first, last).  Returns the first failure and its index. */
static xx_rarx_status_t xx_rarx50_mt_filter_range(xx_rarx50_mt *mt, size_t first, size_t last, xx_pd_struct *progress, size_t *failed_index)
{
    size_t i;
    for (i = first; i < last; ++i) {
        xx_rarx_status_t status;
        if (xx_atomic_load(&mt->abort_flag) != 0) break;
        status = xx_rarx50_apply_filters(mt->filter_destination, mt->filter_destination_size, mt->filter_items + i, 1, progress);
        if (status != XX_RARX_STATUS_OK) {
            *failed_index = i;
            return status;
        }
    }
    return XX_RARX_STATUS_OK;
}

/* Call with the mutex held. */
static void xx_rarx50_mt_filter_failed(xx_rarx50_mt *mt, size_t index, xx_rarx_status_t status)
{
    if (index < mt->filter_error_index) {
        mt->filter_error_index = index;
        mt->filter_error = status;
    }
    xx_atomic_store(&mt->abort_flag, 1);
}

static int xx_rarx50_mt_thread(void *argument)
{
    xx_rarx50_mt_worker *const worker = (xx_rarx50_mt_worker *)argument;
    xx_rarx50_mt *const mt = worker->mt;
    unsigned worker_count;
    size_t k;

    xx_mutex_lock(mt->mutex);
    for (;;) {
        if (mt->stop) break;
        if (mt->claimed < mt->posted) {
            xx_rarx50_mt_slot *const slot = &mt->slots[mt->claimed % mt->slot_count];
            bool ok;
            mt->claimed++;
            mt->active++;
            xx_mutex_unlock(mt->mutex);
            ok = xx_rarx50_mt_tokenize_block(worker, slot);
            xx_mutex_lock(mt->mutex);
            slot->ok = ok;
            slot->done = true;
            mt->active--;
            xx_cond_signal(mt->done_cond);
            continue;
        }
        if (mt->filter_next < mt->filter_count) {
            const size_t first = mt->filter_next;
            const size_t last = mt->filter_count - first > XX_RARX50_MT_FILTER_BATCH ? first + XX_RARX50_MT_FILTER_BATCH : mt->filter_count;
            size_t failed_index = 0;
            xx_rarx_status_t status;
            mt->filter_next = last;
            mt->active++;
            xx_mutex_unlock(mt->mutex);
            status = xx_rarx50_mt_filter_range(mt, first, last, NULL, &failed_index);
            xx_mutex_lock(mt->mutex);
            if (status != XX_RARX_STATUS_OK) xx_rarx50_mt_filter_failed(mt, failed_index, status);
            mt->active--;
            xx_cond_signal(mt->done_cond);
            continue;
        }
        xx_cond_wait(mt->work_cond, mt->mutex, XX_WAIT_INFINITE);
    }
    worker_count = mt->worker_count;
    xx_mutex_unlock(mt->mutex);

    /* The slot buffers hold decoded data: clear them before they are freed.
     * Nothing else touches the slots once stop is set, and the workers share
     * the work. */
    for (k = worker->index; k < mt->slot_count; k += worker_count) {
        xx_rarx50_mt_slot *const slot = &mt->slots[k];
        if (slot->tok) xx_mem_zero(slot->tok, slot->tok_high * sizeof(uint64_t));
        if (slot->lit) xx_mem_zero(slot->lit, slot->lit_high);
    }
    return 0;
}

/* Stops and joins every thread and releases everything. */
static void xx_rarx50_mt_destroy(xx_rarx50_mt *mt)
{
    unsigned i;
    size_t k;

    if (!mt) return;
    if (mt->worker_count != 0) {
        xx_mutex_lock(mt->mutex);
        mt->stop = true;
        xx_mutex_unlock(mt->mutex);
        xx_cond_broadcast(mt->work_cond);
        for (i = 0; i < mt->worker_count; ++i) {
            xx_thread_join(mt->workers[i].thread, NULL);
        }
    }
    for (i = 0; i < XX_RARX50_MT_MAX_THREADS; ++i) {
        xx_mem_free(mt->workers[i].tables);
    }
    xx_mem_free(mt->self.tables);
    if (mt->slots) {
        /* Already cleared by the workers. */
        for (k = 0; k < mt->slot_count; ++k) {
            xx_mem_free(mt->slots[k].tok);
            xx_mem_free(mt->slots[k].lit);
        }
        xx_mem_free(mt->slots);
    }
    if (mt->done_cond) xx_cond_destroy(mt->done_cond);
    if (mt->work_cond) xx_cond_destroy(mt->work_cond);
    if (mt->mutex) xx_mutex_destroy(mt->mutex);
    xx_mem_free(mt);
}

/* Starts the worker threads.  NULL when threads are not available (single
 * processor, no condition variables, no memory, no thread could start). */
static xx_rarx50_mt *xx_rarx50_mt_create(size_t source_left)
{
    const uint32_t processors = xx_thread_cpu_count();
    unsigned wanted;
    unsigned i;
    xx_rarx50_mt *mt;

    if (processors < 2u) return NULL;
    /* The calling thread applies the tokens (and tokenizes when idle). */
    wanted = (processors > XX_RARX50_MT_MAX_THREADS ? XX_RARX50_MT_MAX_THREADS : (unsigned)processors) - 1u;
    if (source_left / XX_RARX50_MT_SOURCE_PER_THREAD < wanted) {
        wanted = (unsigned)(source_left / XX_RARX50_MT_SOURCE_PER_THREAD);
        if (wanted == 0) wanted = 1;
    }
    mt = (xx_rarx50_mt *)xx_mem_calloc(1, sizeof(*mt));
    if (!mt) return NULL;
    mt->mutex = xx_mutex_create();
    mt->work_cond = xx_cond_create();
    mt->done_cond = xx_cond_create();
    mt->slot_count = (size_t)(wanted + 1u) * XX_RARX50_MT_SLOTS_PER_THREAD;
    mt->slots = (xx_rarx50_mt_slot *)xx_mem_calloc(mt->slot_count, sizeof(*mt->slots));
    mt->self.mt = mt;
    mt->self.tables = (xx_rarx50_tables *)xx_mem_alloc(sizeof(xx_rarx50_tables));
    if (!mt->mutex || !mt->work_cond || !mt->done_cond || !mt->slots || !mt->self.tables) {
        xx_rarx50_mt_destroy(mt);
        return NULL;
    }
    for (i = 0; i < wanted; ++i) {
        xx_rarx50_mt_worker *const worker = &mt->workers[mt->worker_count];
        worker->mt = mt;
        worker->index = mt->worker_count;
        worker->tables = (xx_rarx50_tables *)xx_mem_alloc(sizeof(xx_rarx50_tables));
        if (!worker->tables) break;
        worker->thread = xx_thread_create(xx_rarx50_mt_thread, worker);
        if (!worker->thread) break;
        mt->worker_count++;
    }
    if (mt->worker_count == 0) {
        xx_rarx50_mt_destroy(mt);
        return NULL;
    }
    return mt;
}

/* Decodes blocks in parallel from *source_position on (the block there must
 * carry tables).  On return the LZ state, *source_position, *block_count and
 * *saw_last_block describe the blocks applied, exactly as the sequential loop
 * would have left them, and no worker is busy.  Returns OK (the sequential
 * loop continues, or the member is complete), CANCELLED, or the error the
 * sequential loop reports after a block. */
static xx_rarx_status_t xx_rarx50_mt_run(xx_rarx50_mt *mt, xx_rarx50_state *state, const uint8_t *source, size_t source_size, size_t window_size, xx_rarx50_lz *lz,
                                         size_t *source_position, size_t *block_count, bool *saw_last_block, xx_pd_struct *progress)
{
    xx_rarx_status_t status = XX_RARX_STATUS_OK;
    size_t walk_position = *source_position;
    bool walk_end = false;
    const uint8_t *walk_table_payload = NULL;
    size_t walk_table_payload_size = 0;
    size_t walk_table_payload_bits = 0;
    const uint8_t *applied_table_payload = NULL;
    size_t applied_table_payload_size = 0;
    size_t applied_table_payload_bits = 0;
    size_t posted;
    size_t applied;

    xx_mutex_lock(mt->mutex);
    posted = mt->posted;
    xx_mutex_unlock(mt->mutex);
    applied = posted;

    for (;;) {
        xx_rarx50_mt_slot *slot;
        xx_rarx50_lz saved;
        size_t added = 0;

        /* Walk the block headers and post blocks while slots are free. */
        while (!walk_end && posted - applied < mt->slot_count) {
            xx_rarx50_block_header header;
            const uint8_t *payload;
            size_t block_end;

            if (xx_rarx50_read_block_header(source, source_size, walk_position, &header) != XX_RARX_STATUS_OK) {
                walk_end = true;
                break;
            }
            payload = source + walk_position + header.header_size;
            block_end = walk_position + header.header_size + header.payload_size;
            if ((header.flags & 0x80u) != 0) {
                walk_table_payload = payload;
                walk_table_payload_size = header.payload_size;
                walk_table_payload_bits = header.payload_bits;
            }
            /* Left to the sequential loop: oversized blocks, a block without
             * tables at the start of the run, and a block that ends the source
             * without being the last one (a truncated stream). */
            if (header.payload_size > XX_RARX50_MT_MAX_PAYLOAD || !walk_table_payload || ((header.flags & 0x40u) == 0 && block_end >= source_size)) {
                walk_end = true;
                break;
            }
            slot = &mt->slots[posted % mt->slot_count];
            slot->payload = payload;
            slot->payload_size = header.payload_size;
            slot->payload_bits = header.payload_bits;
            slot->table_payload = walk_table_payload;
            slot->table_payload_size = walk_table_payload_size;
            slot->table_payload_bits = walk_table_payload_bits;
            slot->source_end = block_end;
            slot->flags = header.flags;
            slot->ok = false;
            slot->done = false;
            posted++;
            added++;
            walk_position = block_end;
            if ((header.flags & 0x40u) != 0) walk_end = true;
        }
        if (added != 0) {
            xx_mutex_lock(mt->mutex);
            mt->posted = posted;
            xx_mutex_unlock(mt->mutex);
            if (added == 1) {
                xx_cond_signal(mt->work_cond);
            } else {
                xx_cond_broadcast(mt->work_cond);
            }
        }
        if (applied == posted) break;

        /* Wait for the next block in stream order; tokenize others meanwhile. */
        slot = &mt->slots[applied % mt->slot_count];
        xx_mutex_lock(mt->mutex);
        while (!slot->done) {
            if (mt->claimed < mt->posted) {
                xx_rarx50_mt_slot *const other = &mt->slots[mt->claimed % mt->slot_count];
                bool ok;
                mt->claimed++;
                xx_mutex_unlock(mt->mutex);
                ok = xx_rarx50_mt_tokenize_block(&mt->self, other);
                xx_mutex_lock(mt->mutex);
                other->ok = ok;
                other->done = true;
            } else {
                xx_cond_wait(mt->done_cond, mt->mutex, XX_WAIT_INFINITE);
            }
        }
        xx_mutex_unlock(mt->mutex);

        if (xx_rarx50_cancelled(progress)) {
            status = XX_RARX_STATUS_CANCELLED;
            break;
        }
        if (!slot->ok) break;
        saved = *lz;
        if (!xx_rarx50_apply_tokens(state, lz, window_size, slot->tok, slot->tok_count, slot->lit, slot->lit_count)) {
            xx_rarx50_lz_rollback(lz, &saved);
            break;
        }
        xx_rarx50_reps_finish(&slot->reps, lz);
        applied++;
        *source_position = slot->source_end;
        (*block_count)++;
        applied_table_payload = slot->table_payload;
        applied_table_payload_size = slot->table_payload_size;
        applied_table_payload_bits = slot->table_payload_bits;
        if (lz->filter_count == 0) xx_rarx50_stream_output(state, lz);
        if ((slot->flags & 0x40u) != 0) {
            *saw_last_block = true;
            break;
        }
        if (lz->position >= lz->destination_size) {
            status = XX_RARX_STATUS_CORRUPT;
            break;
        }
    }

    /* Take back the blocks nobody started and wait for the busy workers. */
    xx_mutex_lock(mt->mutex);
    mt->posted = mt->claimed;
    xx_atomic_store(&mt->abort_flag, 1);
    while (mt->active != 0) {
        xx_cond_wait(mt->done_cond, mt->mutex, XX_WAIT_INFINITE);
    }
    xx_atomic_store(&mt->abort_flag, 0);
    xx_mutex_unlock(mt->mutex);

    /* The tables of the last applied block become the current tables. */
    if (applied_table_payload) {
        xx_rarx50_tables *const spare = &state->tables[state->table_current ^ 1u];
        xx_rarx50_bits bits;
        xx_rarx_status_t table_status;
        xx_rarx50_bits_init(&bits, applied_table_payload, applied_table_payload_size, applied_table_payload_bits);
        table_status = xx_rarx50_read_tables(spare, &bits);
        if (table_status != XX_RARX_STATUS_OK) {
            /* Not reachable: a worker read the same record successfully. */
            state->tables_valid = false;
            return status != XX_RARX_STATUS_OK ? status : table_status;
        }
        state->table_current ^= 1u;
        state->tables_valid = true;
    }
    return status;
}

/* Applies the filters with the worker threads; the regions do not overlap
 * (xx_rarx50_append_filter and xx_rarx50_add_filter enforce the order). */
static xx_rarx_status_t xx_rarx50_mt_apply_filters(xx_rarx50_mt *mt, uint8_t *destination, size_t destination_size, const xx_rarx50_filter *filters, size_t filter_count,
                                                   xx_pd_struct *progress)
{
    xx_rarx_status_t status;

    xx_mutex_lock(mt->mutex);
    mt->filter_destination = destination;
    mt->filter_destination_size = destination_size;
    mt->filter_items = filters;
    mt->filter_count = filter_count;
    mt->filter_next = 0;
    mt->filter_error_index = SIZE_MAX;
    mt->filter_error = XX_RARX_STATUS_OK;
    xx_mutex_unlock(mt->mutex);
    xx_cond_broadcast(mt->work_cond);

    xx_mutex_lock(mt->mutex);
    while (mt->filter_next < mt->filter_count) {
        const size_t first = mt->filter_next;
        const size_t last = mt->filter_count - first > XX_RARX50_MT_FILTER_BATCH ? first + XX_RARX50_MT_FILTER_BATCH : mt->filter_count;
        size_t failed_index = first;
        mt->filter_next = last;
        xx_mutex_unlock(mt->mutex);
        if (xx_rarx50_cancelled(progress)) {
            status = XX_RARX_STATUS_CANCELLED;
        } else {
            status = xx_rarx50_mt_filter_range(mt, first, last, progress, &failed_index);
        }
        xx_mutex_lock(mt->mutex);
        if (status != XX_RARX_STATUS_OK) {
            xx_rarx50_mt_filter_failed(mt, failed_index, status);
            mt->filter_next = mt->filter_count;
        }
    }
    while (mt->active != 0) {
        xx_cond_wait(mt->done_cond, mt->mutex, XX_WAIT_INFINITE);
    }
    status = mt->filter_error;
    mt->filter_count = 0;
    mt->filter_next = 0;
    mt->filter_items = NULL;
    mt->filter_destination = NULL;
    xx_atomic_store(&mt->abort_flag, 0);
    xx_mutex_unlock(mt->mutex);
    /* The workers do not poll the progress object; when they took every
     * batch, the loop above never checked it.  Report a cancellation that
     * arrived meanwhile like the sequential filter path would. */
    if (status == XX_RARX_STATUS_OK && xx_rarx50_cancelled(progress)) status = XX_RARX_STATUS_CANCELLED;
    return status;
}
#endif /* XX_RARX50_MT */

xx_rarx_status_t xx_rarx50_decode(xx_rarx50_state *state, const uint8_t *source, size_t source_size, uint8_t *destination, size_t destination_size, size_t window_size,
                                  bool solid, size_t *source_used, xx_pd_struct *progress)
{
    xx_rarx50_lz lz;
    size_t source_position = 0;
    size_t block_count = 0;
    bool saw_last_block = false;
    bool token_path;
    xx_rarx_status_t status = XX_RARX_STATUS_OK;
#if XX_RARX50_MT
    xx_rarx50_mt *mt = NULL;
    bool mt_usable = true;
    bool mt_skip = false;
    unsigned mt_runs = 0;
#endif

    if (source_used) *source_used = 0;
    if (!state || (!source && source_size != 0) || (!destination && destination_size != 0) || window_size == 0) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    if (xx_rarx50_cancelled(progress)) {
        return XX_RARX_STATUS_CANCELLED;
    }
    if (!solid) {
        xx_rarx50_reset(state);
        state->window_size = window_size;
    } else if (state->window_size == 0 || state->window_size != window_size) {
        return XX_RARX_STATUS_CORRUPT;
    }

    lz.destination = destination;
    lz.destination_size = destination_size;
    lz.position = 0;
    lz.dist0 = state->repeated_distance[0];
    lz.dist1 = state->repeated_distance[1];
    lz.dist2 = state->repeated_distance[2];
    lz.dist3 = state->repeated_distance[3];
    lz.last_len = state->last_length;
    lz.filters = NULL;
    lz.filter_count = 0;
    lz.filter_capacity = 0;
    token_path = xx_rarx50_token_scratch(state);

    while (!saw_last_block) {
        xx_rarx50_block_header header;
        const xx_rarx50_tables *tables;
        xx_rarx50_bits bits;
        bool decoded = false;

        if (xx_rarx50_cancelled(progress)) {
            status = XX_RARX_STATUS_CANCELLED;
            goto cleanup;
        }
#if XX_RARX50_MT
        /* A parallel run starts at a block with tables.  After a run the next
         * block is always decoded here, so the loop makes progress. */
        if (mt_usable && !mt_skip && source_position < source_size && source_size - source_position >= XX_RARX50_MT_MIN_SOURCE &&
            (source[source_position] & 0x80u) != 0) {
            if (!mt) {
                mt = xx_rarx50_mt_create(source_size - source_position);
                if (!mt) mt_usable = false;
            }
            if (mt) {
                if (++mt_runs >= XX_RARX50_MT_MAX_RUNS) mt_usable = false;
                status = xx_rarx50_mt_run(mt, state, source, source_size, window_size, &lz, &source_position, &block_count, &saw_last_block, progress);
                if (status != XX_RARX_STATUS_OK) goto cleanup;
                mt_skip = true;
                continue;
            }
        }
        mt_skip = false;
#endif
        status = xx_rarx50_read_block_header(source, source_size, source_position, &header);
        if (status != XX_RARX_STATUS_OK) goto cleanup;
        xx_rarx50_bits_init(&bits, source + source_position + header.header_size, header.payload_size, header.payload_bits);
        source_position += header.header_size + header.payload_size;
        block_count++;

        if ((header.flags & 0x80u) != 0) {
            status = xx_rarx50_read_tables(&state->tables[state->table_current ^ 1u], &bits);
            if (status != XX_RARX_STATUS_OK) goto cleanup;
            state->table_current ^= 1u;
            state->tables_valid = true;
        } else if (!state->tables_valid) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        tables = &state->tables[state->table_current];

        if (token_path) {
            const xx_rarx50_bits saved_bits = bits;
            const xx_rarx50_lz saved_lz = lz;
            decoded = xx_rarx50_decode_block_tokens(state, tables, &lz, window_size, &bits);
            if (!decoded) {
                bits = saved_bits;
                xx_rarx50_lz_rollback(&lz, &saved_lz);
            }
        }
        if (!decoded) {
            status = xx_rarx50_decode_block(state, tables, &lz, window_size, &bits);
            if (status != XX_RARX_STATUS_OK) goto cleanup;
        }
        if (lz.filter_count == 0) xx_rarx50_stream_output(state, &lz);
        if (bits.bit_position != bits.bit_limit) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        saw_last_block = (header.flags & 0x40u) != 0;
        if (!saw_last_block && lz.position >= destination_size) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        if (!saw_last_block && source_position >= source_size) {
            status = XX_RARX_STATUS_TRUNCATED;
            goto cleanup;
        }
        if (block_count > source_size / 3u + 1u) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
    }

    if (lz.position != destination_size) {
        status = XX_RARX_STATUS_TRUNCATED;
        goto cleanup;
    }
    state->repeated_distance[0] = lz.dist0;
    state->repeated_distance[1] = lz.dist1;
    state->repeated_distance[2] = lz.dist2;
    state->repeated_distance[3] = lz.dist3;
    state->last_length = lz.last_len;

    status = xx_rarx50_remember_history(state, destination, destination_size, window_size);
    if (status != XX_RARX_STATUS_OK) goto cleanup;
#if XX_RARX50_MT
    if (mt && lz.filter_count >= XX_RARX50_MT_MIN_FILTERS) {
        status = xx_rarx50_mt_apply_filters(mt, destination, destination_size, lz.filters, lz.filter_count, progress);
    } else
#endif
    {
        status = xx_rarx50_apply_filters(destination, destination_size, lz.filters, lz.filter_count, progress);
    }
    if (status != XX_RARX_STATUS_OK) goto cleanup;
    xx_rarx50_stream_output(state, &lz);
    if (source_used) *source_used = source_position;

cleanup:
#if XX_RARX50_MT
    xx_rarx50_mt_destroy(mt);
#endif
    state->repeated_distance[0] = lz.dist0;
    state->repeated_distance[1] = lz.dist1;
    state->repeated_distance[2] = lz.dist2;
    state->repeated_distance[3] = lz.dist3;
    state->last_length = lz.last_len;
    if (lz.filters) xx_rarx_clear_free(lz.filters, lz.filter_capacity * sizeof(*lz.filters));
    return status;
}
