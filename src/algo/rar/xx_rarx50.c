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
#define XX_RARX50_MAX_FILTER_SIZE (1u << 22)
#define XX_RARX50_X86_FILE_SIZE 0x01000000u
#define XX_RARX50_POLL_MASK 0x3fffu

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
    uint16_t fast_table[XX_RARX50_FAST_SIZE];
    uint16_t symbol_count;
} xx_rarx50_huffman;

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

struct xx_rarx50_state {
    xx_rarx50_huffman main_table;
    xx_rarx50_huffman distance_table;
    xx_rarx50_huffman align_table;
    xx_rarx50_huffman length_table;
    bool tables_valid;
    bool align_mode;
    size_t repeated_distance[4];
    size_t last_length;
    uint8_t *history;
    size_t history_size;
    size_t history_capacity;
    size_t window_size;
    xx_rarx_chunk_cb chunk_cb;
    void *chunk_user_data;
    size_t stream_pos;
};

static bool xx_rarx50_cancelled(xx_pd_struct *progress)
{
    return progress && xx_pd_is_stopped(progress);
}

#if defined(_MSC_VER)
#define XX_FORCE_INLINE static __forceinline
#define XX_NOINLINE static __declspec(noinline)
#define XX_RARX50_BSWAP32(w) _byteswap_ulong(w)
#elif defined(__GNUC__) || defined(__clang__)
#define XX_FORCE_INLINE static inline __attribute__((always_inline))
#define XX_NOINLINE static __attribute__((noinline))
#define XX_RARX50_BSWAP32(w) __builtin_bswap32(w)
#else
#define XX_FORCE_INLINE static inline
#define XX_NOINLINE static
#define XX_RARX50_BSWAP32(w) ((((uint32_t)(w) & 0xff000000u) >> 24) | (((uint32_t)(w) & 0x00ff0000u) >> 8) | (((uint32_t)(w) & 0x0000ff00u) << 8) | (((uint32_t)(w) & 0x000000ffu) << 24))
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

XX_FORCE_INLINE uint32_t xx_rarx50_peek_bits(const xx_rarx50_bits *bits, unsigned count)
{
    if (count == 0) return 0;
    return (uint32_t)(bits->buf >> (64u - count));
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

static xx_rarx_status_t xx_rarx50_build_huffman(xx_rarx50_huffman *table, const uint8_t *lengths, size_t length_count)
{
    uint16_t next_index[XX_RARX50_MAX_CODE_BITS + 1u];
    int32_t available = 1;
    uint32_t code = 0;
    size_t total = 0;
    size_t i;
    unsigned length;

    if (!table || !lengths || length_count > XX_RARX50_MAX_HUFF_SYMBOLS) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    xx_mem_zero(table, sizeof(*table));
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

    /* Build fast lookup table for codes with length <= XX_RARX50_FAST_BITS */
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
            uint16_t cur_entry = (uint16_t)((sym << 4) | length);
            uint32_t base_pattern = sym_code << shift;
            uint32_t p = 0;

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

XX_FORCE_INLINE xx_rarx_status_t xx_rarx50_decode_huffman(const xx_rarx50_huffman *table, xx_rarx50_bits *bits, unsigned *symbol)
{
    uint32_t peek = (uint32_t)(bits->buf >> (64u - XX_RARX50_FAST_BITS));
    uint32_t entry = table->fast_table[peek];
    unsigned length = entry & 0x0Fu;
    if (length != 0) {
        *symbol = entry >> 4;
        xx_rarx50_consume_bits(bits, length);
        return XX_RARX_STATUS_OK;
    }
    return xx_rarx50_decode_huffman_slow(table, bits, symbol);
}

static xx_rarx_status_t xx_rarx50_read_tables(xx_rarx50_state *state, xx_rarx50_bits *bits)
{
    uint8_t level_lengths[XX_RARX50_LEVEL_SYMBOLS];
    uint8_t lengths[XX_RARX50_TOTAL_LENGTHS];
    xx_rarx50_huffman level_table;
    xx_rarx50_huffman main_table;
    xx_rarx50_huffman distance_table;
    xx_rarx50_huffman align_table;
    xx_rarx50_huffman length_table;
    size_t position = 0;
    size_t i;
    xx_rarx_status_t status;

    xx_mem_zero(level_lengths, sizeof(level_lengths));
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

    position = 0;
    while (position < XX_RARX50_TOTAL_LENGTHS) {
        unsigned symbol;
        size_t count;
        uint32_t extra;
        status = xx_rarx50_decode_huffman(&level_table, bits, &symbol);
        if (status != XX_RARX_STATUS_OK) return status;
        if (symbol <= 15u) {
            lengths[position++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16u || symbol == 17u) {
            if (position == 0) return XX_RARX_STATUS_CORRUPT;
            status = xx_rarx50_read_bits(bits, symbol == 16u ? 3u : 7u, &extra);
            if (status != XX_RARX_STATUS_OK) return status;
            count = (symbol == 16u ? 3u : 11u) + (size_t)extra;
            if (count > XX_RARX50_TOTAL_LENGTHS - position) {
                count = XX_RARX50_TOTAL_LENGTHS - position;
            }
            for (i = 0; i < count; ++i) {
                lengths[position] = lengths[position - 1u];
                position++;
            }
            continue;
        }
        if (symbol == 18u || symbol == 19u) {
            status = xx_rarx50_read_bits(bits, symbol == 18u ? 3u : 7u, &extra);
            if (status != XX_RARX_STATUS_OK) return status;
            count = (symbol == 18u ? 3u : 11u) + (size_t)extra;
            if (count > XX_RARX50_TOTAL_LENGTHS - position) {
                count = XX_RARX50_TOTAL_LENGTHS - position;
            }
            xx_mem_zero(lengths + position, count);
            position += count;
            continue;
        }
        return XX_RARX_STATUS_CORRUPT;
    }

    status = xx_rarx50_build_huffman(&main_table, lengths, XX_RARX50_MAIN_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_build_huffman(&distance_table, lengths + XX_RARX50_MAIN_SYMBOLS, XX_RARX50_DISTANCE_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_build_huffman(&align_table, lengths + XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS, XX_RARX50_ALIGN_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;
    status = xx_rarx50_build_huffman(&length_table, lengths + XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS + XX_RARX50_ALIGN_SYMBOLS, XX_RARX50_LENGTH_SYMBOLS);
    if (status != XX_RARX_STATUS_OK) return status;

    state->main_table = main_table;
    state->distance_table = distance_table;
    state->align_table = align_table;
    state->length_table = length_table;
    state->align_mode = false;
    for (i = XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS; i < XX_RARX50_MAIN_SYMBOLS + XX_RARX50_DISTANCE_SYMBOLS + XX_RARX50_ALIGN_SYMBOLS; ++i) {
        if (lengths[i] != 0 && lengths[i] != 4) {
            state->align_mode = true;
            break;
        }
    }
    state->tables_valid = true;
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

static xx_rarx_status_t xx_rarx50_append_filter(xx_rarx50_filter **filters, size_t *filter_count, size_t current_position, size_t output_size, xx_rarx50_bits *bits)
{
    xx_rarx50_filter filter;
    xx_rarx50_filter *resized;
    uint32_t offset;
    uint32_t length;
    uint32_t type;
    xx_rarx_status_t status;

    if (!filters || !filter_count || !bits) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    if (*filter_count >= XX_RARX50_MAX_FILTERS) {
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
    if (*filter_count != 0) {
        const xx_rarx50_filter *previous = &(*filters)[*filter_count - 1u];
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

    if (*filter_count == SIZE_MAX / sizeof(**filters)) {
        return XX_RARX_STATUS_LIMIT;
    }
    resized = (xx_rarx50_filter *)xx_rarx_clear_resize(*filters, *filter_count * sizeof(**filters), (*filter_count + 1u) * sizeof(**filters));
    if (!resized) {
        return XX_RARX_STATUS_NO_MEMORY;
    }
    resized[*filter_count] = filter;
    *filters = resized;
    (*filter_count)++;
    return XX_RARX_STATUS_OK;
}

static const uint8_t xx_rarx50_length_bits[44] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9
};
static const uint32_t xx_rarx50_length_base[44] = {
    2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 22, 26, 30, 34, 42, 50, 58, 66, 82, 98, 114, 130, 162, 194, 226, 258, 322, 386, 450, 514, 642, 770, 898, 1026, 1282, 1538, 1794, 2050, 2562, 3074, 3586
};
static const uint8_t xx_rarx50_dist_bits[64] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30
};
static const uint32_t xx_rarx50_dist_base[64] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 32769, 49153, 65537, 98305, 131073, 196609, 262145, 393217, 524289, 786433, 1048577, 1572865, 2097153, 3145729, 4194305, 6291457, 8388609, 12582913, 16777217, 25165825, 33554433, 50331649, 67108865, 100663297, 134217729, 201326593, 268435457, 402653185, 536870913, 805306369, 1073741825, 1610612737, 2147483649, 3221225473
};

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

XX_FORCE_INLINE xx_rarx_status_t xx_rarx50_distance_from_slot(unsigned slot, const xx_rarx50_huffman *align_table, bool align_mode, xx_rarx50_bits *bits, size_t *distance)
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
    const __m128i v_eb = _mm_set1_epi8((char)0xeb);
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
    xx_mem_free(state);
}

void xx_rarx50_reset(xx_rarx50_state *state)
{
    uint8_t *history;
    size_t capacity;
    xx_rarx_chunk_cb chunk_cb;
    void *chunk_user_data;
    if (!state) return;
    history = state->history;
    capacity = state->history_capacity;
    chunk_cb = state->chunk_cb;
    chunk_user_data = state->chunk_user_data;
    xx_mem_zero(history, capacity);
    xx_mem_zero(state, sizeof(*state));
    state->history = history;
    state->history_capacity = capacity;
    state->chunk_cb = chunk_cb;
    state->chunk_user_data = chunk_user_data;
    state->stream_pos = 0;
}

void xx_rarx50_set_chunk_callback(xx_rarx50_state *state, xx_rarx_chunk_cb cb, void *user_data)
{
    if (!state) return;
    state->chunk_cb = cb;
    state->chunk_user_data = user_data;
    state->stream_pos = 0;
}

XX_NOINLINE xx_rarx_status_t xx_rarx50_decode_block(
    const xx_rarx50_state *state,
    uint8_t *destination,
    size_t *p_dest_pos,
    size_t destination_size,
    size_t window_size,
    xx_rarx50_bits *bits,
    xx_rarx50_filter **p_filters,
    size_t *p_filter_count,
    size_t *p_dist0,
    size_t *p_dist1,
    size_t *p_dist2,
    size_t *p_dist3,
    size_t *p_last_len)
{
    uint8_t *dst = destination + *p_dest_pos;
    const uint8_t *const dst_end = destination + destination_size;
    size_t dist0 = *p_dist0;
    size_t dist1 = *p_dist1;
    size_t dist2 = *p_dist2;
    size_t dist3 = *p_dist3;
    size_t last_len = *p_last_len;

    const uint16_t *const main_fast = state->main_table.fast_table;
    const uint16_t *const length_fast = state->length_table.fast_table;
    const uint16_t *const dist_fast = state->distance_table.fast_table;
    const uint16_t *const align_fast = state->align_table.fast_table;
    const bool align_mode = state->align_mode;

    uint64_t bit_buf = bits->buf;
    int bits_left = bits->bits_left;
    size_t bit_pos = bits->bit_position;
    const size_t bit_limit = bits->bit_limit;
    const uint8_t *bit_src = bits->src;
    const uint8_t *const bit_src_end = bits->src_end;
    xx_rarx_status_t status = XX_RARX_STATUS_OK;

#define XX_RARX50_CONSUME_FAST(count) do { \
    unsigned _cnt = (unsigned)(count); \
    bit_buf <<= _cnt; \
    bits_left -= (int)_cnt; \
    if (bits_left < 32) { \
        uint32_t _w = xx_mem_load32(bit_src); \
        _w = XX_RARX50_BSWAP32(_w); \
        bit_buf |= ((uint64_t)_w) << (32 - bits_left); \
        bit_src += 4; \
        bits_left += 32; \
    } \
} while (0)

#define XX_RARX50_SYNC_BITS() do { \
    bit_pos = anchor_bit_pos + ((size_t)(bit_src - anchor_src) * 8u) + (size_t)(anchor_bits_left - bits_left); \
    bits->buf = bit_buf; bits->bits_left = bits_left; bits->bit_position = bit_pos; bits->src = bit_src; \
} while (0)

#define XX_RARX50_RESYNC_BITS() do { \
    bit_buf = bits->buf; bits_left = bits->bits_left; bit_pos = bits->bit_position; bit_src = bits->src; \
    anchor_src = bit_src; anchor_bits_left = bits_left; anchor_bit_pos = bit_pos; \
} while (0)

#define XX_RARX50_CONSUME_SAFE(count) do { \
    unsigned _cnt = (unsigned)(count); \
    bit_buf <<= _cnt; \
    bits_left -= (int)_cnt; \
    bit_pos += _cnt; \
    if (bits_left < 32) { \
        if (bit_src + 4 <= bit_src_end) { \
            uint32_t _w = xx_mem_load32(bit_src); \
            _w = XX_RARX50_BSWAP32(_w); \
            bit_buf |= ((uint64_t)_w) << (32 - bits_left); \
            bit_src += 4; \
            bits_left += 32; \
        } else { \
            while (bits_left <= 56 && bit_src < bit_src_end) { \
                bit_buf |= ((uint64_t)*bit_src++) << (56 - bits_left); \
                bits_left += 8; \
            } \
        } \
    } \
} while (0)

#define XX_RARX50_PERFORM_MATCH(dist, len) do { \
    size_t _distance = (dist); \
    size_t _length = (len); \
    if (dst + _length <= dst_end && _distance != 0 && _distance <= (size_t)(dst - destination)) { \
        xx_rarx50_copy_match_fast_ptr(dst, _distance, _length, dst_end); \
        dst += _length; \
    } else { \
        size_t current_pos = (size_t)(dst - destination); \
        status = xx_rarx50_copy_match(state, destination, &current_pos, destination_size, window_size, _distance, _length); \
        if (status != XX_RARX_STATUS_OK) goto done; \
        dst = destination + current_pos; \
    } \
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
        unsigned hlen = entry & 0x0Fu;
        unsigned symbol;
        if (hlen != 0) {
            symbol = entry >> 4;
            XX_RARX50_CONSUME_FAST(hlen);
        } else {
            XX_RARX50_SYNC_BITS();
            status = xx_rarx50_decode_huffman_slow(&state->main_table, bits, &symbol);
            if (status != XX_RARX_STATUS_OK) goto done;
            XX_RARX50_RESYNC_BITS();
        }
        if (symbol < 256u) {
            *dst++ = (uint8_t)symbol;
            continue;
        } else if (symbol == 256u) {
            XX_RARX50_SYNC_BITS();
            status = xx_rarx50_append_filter(p_filters, p_filter_count, (size_t)(dst - destination), destination_size, bits);
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
                distance = dist1; dist1 = dist0;
            } else if (symbol == 260u) {
                distance = dist2; dist2 = dist1; dist1 = dist0;
            } else {
                distance = dist3; dist3 = dist2; dist2 = dist1; dist1 = dist0;
            }
            dist0 = distance;
            if (distance == 0) {
                status = XX_RARX_STATUS_CORRUPT;
                goto done;
            }
            peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
            entry = length_fast[peek];
            hlen = entry & 0x0Fu;
            if (hlen != 0) {
                length_slot = entry >> 4;
                XX_RARX50_CONSUME_FAST(hlen);
            } else {
                XX_RARX50_SYNC_BITS();
                status = xx_rarx50_decode_huffman_slow(&state->length_table, bits, &length_slot);
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
            hlen = entry & 0x0Fu;
            if (hlen != 0) {
                distance_slot = entry >> 4;
                XX_RARX50_CONSUME_FAST(hlen);
            } else {
                XX_RARX50_SYNC_BITS();
                status = xx_rarx50_decode_huffman_slow(&state->distance_table, bits, &distance_slot);
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
                    hlen = entry & 0x0Fu;
                    unsigned low;
                    if (hlen != 0) {
                        low = entry >> 4;
                        XX_RARX50_CONSUME_FAST(hlen);
                    } else {
                        XX_RARX50_SYNC_BITS();
                        status = xx_rarx50_decode_huffman_slow(&state->align_table, bits, &low);
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
        unsigned hlen = entry & 0x0Fu;
        unsigned symbol;
        if (hlen != 0) {
            symbol = entry >> 4;
            XX_RARX50_CONSUME_SAFE(hlen);
        } else {
            bits->buf = bit_buf; bits->bits_left = bits_left; bits->bit_position = bit_pos; bits->src = bit_src;
            status = xx_rarx50_decode_huffman_slow(&state->main_table, bits, &symbol);
            if (status != XX_RARX_STATUS_OK) goto done;
            bit_buf = bits->buf; bits_left = bits->bits_left; bit_pos = bits->bit_position; bit_src = bits->src;
        }
        if (symbol < 256u) {
            if (dst >= dst_end) {
                status = XX_RARX_STATUS_CORRUPT;
                goto done;
            }
            *dst++ = (uint8_t)symbol;
            continue;
        } else if (symbol == 256u) {
            bits->buf = bit_buf; bits->bits_left = bits_left; bits->bit_position = bit_pos; bits->src = bit_src;
            status = xx_rarx50_append_filter(p_filters, p_filter_count, (size_t)(dst - destination), destination_size, bits);
            if (status != XX_RARX_STATUS_OK) goto done;
            bit_buf = bits->buf; bits_left = bits->bits_left; bit_pos = bits->bit_position; bit_src = bits->src;
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
                distance = dist1; dist1 = dist0;
            } else if (symbol == 260u) {
                distance = dist2; dist2 = dist1; dist1 = dist0;
            } else {
                distance = dist3; dist3 = dist2; dist2 = dist1; dist1 = dist0;
            }
            dist0 = distance;
            if (distance == 0) {
                status = XX_RARX_STATUS_CORRUPT;
                goto done;
            }
            peek = (uint32_t)(bit_buf >> (64u - XX_RARX50_FAST_BITS));
            entry = length_fast[peek];
            hlen = entry & 0x0Fu;
            if (hlen != 0) {
                length_slot = entry >> 4;
                XX_RARX50_CONSUME_SAFE(hlen);
            } else {
                bits->buf = bit_buf; bits->bits_left = bits_left; bits->bit_position = bit_pos; bits->src = bit_src;
                status = xx_rarx50_decode_huffman_slow(&state->length_table, bits, &length_slot);
                if (status != XX_RARX_STATUS_OK) goto done;
                bit_buf = bits->buf; bits_left = bits->bits_left; bit_pos = bits->bit_position; bit_src = bits->src;
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
            hlen = entry & 0x0Fu;
            if (hlen != 0) {
                distance_slot = entry >> 4;
                XX_RARX50_CONSUME_SAFE(hlen);
            } else {
                bits->buf = bit_buf; bits->bits_left = bits_left; bits->bit_position = bit_pos; bits->src = bit_src;
                status = xx_rarx50_decode_huffman_slow(&state->distance_table, bits, &distance_slot);
                if (status != XX_RARX_STATUS_OK) goto done;
                bit_buf = bits->buf; bits_left = bits->bits_left; bit_pos = bits->bit_position; bit_src = bits->src;
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
                    hlen = entry & 0x0Fu;
                    unsigned low;
                    if (hlen != 0) {
                        low = entry >> 4;
                        XX_RARX50_CONSUME_SAFE(hlen);
                    } else {
                        bits->buf = bit_buf; bits->bits_left = bits_left; bits->bit_position = bit_pos; bits->src = bit_src;
                        status = xx_rarx50_decode_huffman_slow(&state->align_table, bits, &low);
                        if (status != XX_RARX_STATUS_OK) goto done;
                        bit_buf = bits->buf; bits_left = bits->bits_left; bit_pos = bits->bit_position; bit_src = bits->src;
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
    *p_dest_pos = (size_t)(dst - destination);
    *p_dist0 = dist0;
    *p_dist1 = dist1;
    *p_dist2 = dist2;
    *p_dist3 = dist3;
    *p_last_len = last_len;
    return status;

#undef XX_RARX50_CONSUME_FAST
#undef XX_RARX50_CONSUME_SAFE
#undef XX_RARX50_PERFORM_MATCH
}

xx_rarx_status_t xx_rarx50_decode(xx_rarx50_state *state, const uint8_t *source, size_t source_size, uint8_t *destination, size_t destination_size, size_t window_size,
                                  bool solid, size_t *source_used, xx_pd_struct *progress)
{
    xx_rarx50_filter *filters = NULL;
    size_t filter_count = 0;
    size_t source_position = 0;
    size_t destination_position = 0;
    size_t block_count = 0;
    bool saw_last_block = false;
    xx_rarx_status_t status = XX_RARX_STATUS_OK;
    size_t dist0;
    size_t dist1;
    size_t dist2;
    size_t dist3;
    size_t last_len;

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

    dist0 = state->repeated_distance[0];
    dist1 = state->repeated_distance[1];
    dist2 = state->repeated_distance[2];
    dist3 = state->repeated_distance[3];
    last_len = state->last_length;

    while (!saw_last_block) {
        uint8_t flags;
        uint8_t checksum;
        unsigned size_width_code;
        size_t size_width;
        size_t header_size;
        size_t payload_size = 0;
        size_t payload_bits;
        size_t i;
        xx_rarx50_bits bits;

        if (xx_rarx50_cancelled(progress)) {
            status = XX_RARX_STATUS_CANCELLED;
            goto cleanup;
        }
        if (source_position > source_size || source_size - source_position < 3u) {
            status = XX_RARX_STATUS_TRUNCATED;
            goto cleanup;
        }
        flags = source[source_position];
        checksum = source[source_position + 1u];
        size_width_code = (unsigned)((flags >> 3) & 3u);
        if (size_width_code == 3u) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        size_width = (size_t)size_width_code + 1u;
        header_size = 2u + size_width;
        if (header_size > source_size - source_position) {
            status = XX_RARX_STATUS_TRUNCATED;
            goto cleanup;
        }
        for (i = 0; i < size_width; ++i) {
            uint8_t byte_value = source[source_position + 2u + i];
            checksum ^= byte_value;
            payload_size |= (size_t)byte_value << (i * 8u);
        }
        checksum ^= flags;
        if (checksum != 0x5au) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        if (payload_size > source_size - source_position - header_size) {
            status = XX_RARX_STATUS_TRUNCATED;
            goto cleanup;
        }
        if (payload_size == 0) {
            payload_bits = 0;
        } else {
            unsigned final_bits = (unsigned)(flags & 7u) + 1u;
            payload_bits = (payload_size - 1u) * 8u + final_bits;
        }
        bits.src = source + source_position + header_size;
        bits.src_end = bits.src + payload_size;
        bits.buf = 0;
        bits.bits_left = 0;
        bits.bit_position = 0;
        bits.bit_limit = payload_bits;
        xx_rarx50_refill(&bits);
        source_position += header_size + payload_size;
        block_count++;

        if ((flags & 0x80u) != 0) {
            status = xx_rarx50_read_tables(state, &bits);
            if (status != XX_RARX_STATUS_OK) goto cleanup;
        } else if (!state->tables_valid) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }

        status = xx_rarx50_decode_block(state, destination, &destination_position, destination_size, window_size,
                                         &bits, &filters, &filter_count,
                                         &dist0, &dist1, &dist2, &dist3, &last_len);
        if (status != XX_RARX_STATUS_OK) goto cleanup;
        if (filter_count == 0 && state->chunk_cb && destination_position > state->stream_pos) {
            state->chunk_cb(state->chunk_user_data, destination + state->stream_pos, destination_position - state->stream_pos);
            state->stream_pos = destination_position;
        }
        if (bits.bit_position != bits.bit_limit) {
            status = XX_RARX_STATUS_CORRUPT;
            goto cleanup;
        }
        saw_last_block = (flags & 0x40u) != 0;
        if (!saw_last_block && destination_position >= destination_size) {
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

    if (destination_position != destination_size) {
        status = XX_RARX_STATUS_TRUNCATED;
        goto cleanup;
    }
    state->repeated_distance[0] = dist0;
    state->repeated_distance[1] = dist1;
    state->repeated_distance[2] = dist2;
    state->repeated_distance[3] = dist3;
    state->last_length = last_len;

    status = xx_rarx50_remember_history(state, destination, destination_size, window_size);
    if (status != XX_RARX_STATUS_OK) goto cleanup;
    status = xx_rarx50_apply_filters(destination, destination_size, filters, filter_count, progress);
    if (status != XX_RARX_STATUS_OK) goto cleanup;
    if (state->chunk_cb && destination_position > state->stream_pos) {
        state->chunk_cb(state->chunk_user_data, destination + state->stream_pos, destination_position - state->stream_pos);
        state->stream_pos = destination_position;
    }
    if (source_used) *source_used = source_position;

cleanup:
    state->repeated_distance[0] = dist0;
    state->repeated_distance[1] = dist1;
    state->repeated_distance[2] = dist2;
    state->repeated_distance[3] = dist3;
    state->last_length = last_len;
    xx_rarx_clear_free(filters, filter_count * sizeof(*filters));
    return status;
}
