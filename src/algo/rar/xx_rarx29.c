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
 * RAR 2.9/3.x/4.x (unpack version 29) decoder.
 *
 * This is an independent C implementation based on the public wire-format
 * description in bitplane/rar-research and interoperability observations from
 * permissively licensed readers.  It does not contain unRAR source code.
 *
 * Implemented here:
 *   - the complete canonical-Huffman/LZ stream,
 *   - solid dictionary, table and filter-program continuation,
 *   - all six standard RAR3 filters selected by bytecode fingerprints.
 *
 * PPMd-H blocks with model orders through 64 are decoded with the existing
 * public-domain PPMd-H model core.  Non-standard RARVM bytecode is deliberately
 * refused with an explicit unsupported status.  Encryption and volume joining
 * belong to the archive/container layer and are not part of this decoder.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xx_rarx_internal.h"
#include "../ppmd7/xx_ppmd7_internal.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

#define XX_RAR29_MAIN_SYMBOLS 299u
#define XX_RAR29_DISTANCE_SYMBOLS 60u
#define XX_RAR29_LOW_SYMBOLS 17u
#define XX_RAR29_LENGTH_SYMBOLS 28u
#define XX_RAR29_LEVEL_SYMBOLS 20u
#define XX_RAR29_TABLE_LENGTHS 404u
#define XX_RAR29_MAX_BITS 15u
#define XX_RAR29_MAX_PROGRAMS 1024u
#define XX_RAR29_MAX_FILTERS 8192u
#define XX_RAR29_FILTER_BYTES_PER_SLOT 256u
#define XX_RAR29_VM_WORK_SIZE 0x3c000u
#define XX_RAR29_VM_HALF_SIZE 0x20000u
#define XX_RAR29_PPM_MAX_ORDER 64u

typedef struct xx_rar29_bits_s {
    const uint8_t *data;
    size_t size;
    size_t bit;
    xx_rarx_status_t error;
} xx_rar29_bits;

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#define XX_RAR29_USE_SSE2 1
#endif

/* Direct-lookup widths of the per-alphabet decode tables.  An entry is
 * (symbol << 4) | code_length; XX_RAR29_FAST_INVALID marks an index whose code
 * is longer than the table width (or unassigned) and needs the canonical
 * search in xx_rar29_decode_symbol_slow. */
#define XX_RAR29_FAST_BITS_MAX 11u
#define XX_RAR29_FAST_SIZE (1u << XX_RAR29_FAST_BITS_MAX)
#define XX_RAR29_MAIN_FAST_BITS 11u
#define XX_RAR29_DISTANCE_FAST_BITS 9u
#define XX_RAR29_LOW_FAST_BITS 7u
#define XX_RAR29_LENGTH_FAST_BITS 8u
#define XX_RAR29_LEVEL_FAST_BITS 7u
#define XX_RAR29_FAST_INVALID 0xffffu

/* The fast LZ loop reads the main and the distance alphabet through packed
 * 32-bit tables derived from the 16-bit ones, so that one load gives
 * everything needed to consume a symbol together with its extra bits:
 *   main:     symbol << 18 | base << 9 | extra_bits << 6 | (code_length + extra_bits)
 *             base/extra_bits describe the match length (symbols 271..) or the
 *             short distance (263..270) and are zero for every other symbol;
 *   distance: (slot base + 1) << 10 | direct_bits << 6 | (code_length + direct_bits)
 *             direct_bits are the bits read straight from the stream (for
 *             slots above 9 the low four bits are coded separately).
 * The low six bits are always the number of bits to drop. */
#define XX_RAR29_PACKED_INVALID 0xffffffffu
#define XX_RAR29_PACKED_SYMBOL_SHIFT 18u
/* Main entries below this value are literals (symbol < 256). */
#define XX_RAR29_PACKED_LITERAL_END (256u << XX_RAR29_PACKED_SYMBOL_SHIFT)

/* The fast LZ loop runs only while this much input and output remains, so one
 * iteration (five literals, or up to two literals plus one match; two 8-byte refills; wide
 * copies that overshoot the match end by up to 30 bytes) cannot leave either
 * buffer.  The longest LZ match is 260 bytes. */
#define XX_RAR29_FAST_INPUT_MARGIN 16u
#define XX_RAR29_FAST_OUTPUT_MARGIN 320u
#define XX_RAR29_FAST_MAX_MATCH 260u
#define XX_RAR29_FAST_BURST ((size_t)1u << 20)
#define XX_RAR29_FAST_IDLE_LIMIT ((size_t)1u << 16)

typedef struct xx_rar29_huffman_s {
    uint16_t count[XX_RAR29_MAX_BITS + 1u];
    uint16_t first_symbol[XX_RAR29_MAX_BITS + 1u];
    uint16_t symbols[XX_RAR29_TABLE_LENGTHS];
    uint32_t first_code[XX_RAR29_MAX_BITS + 1u];
    uint16_t fast_table[XX_RAR29_FAST_SIZE];
    uint16_t symbol_count;
    uint8_t fast_bits;
    bool valid;
} xx_rar29_huffman;

enum { XX_RAR29_CODE_MAIN = 0, XX_RAR29_CODE_DISTANCE, XX_RAR29_CODE_LOW, XX_RAR29_CODE_LENGTH, XX_RAR29_CODE_COUNT };

typedef enum xx_rar29_filter_type_e {
    XX_RAR29_FILTER_E8 = 1,
    XX_RAR29_FILTER_E8E9,
    XX_RAR29_FILTER_ITANIUM,
    XX_RAR29_FILTER_DELTA,
    XX_RAR29_FILTER_RGB,
    XX_RAR29_FILTER_AUDIO
} xx_rar29_filter_type;

typedef struct xx_rar29_program_s {
    xx_rar29_filter_type type;
    uint32_t previous_length;
    uint32_t executions;
} xx_rar29_program;

typedef struct xx_rar29_filter_s {
    size_t start;
    size_t length;
    xx_rar29_filter_type type;
    uint32_t registers[7];
} xx_rar29_filter;

typedef struct xx_rar29_filter_list_s {
    xx_rar29_filter *items;
    size_t count;
    size_t capacity;
} xx_rar29_filter_list;

typedef struct xx_rar29_ppm_range_s {
    xx_rar29_bits *input;
    uint32_t range;
    uint32_t code;
    uint32_t low;
} xx_rar29_ppm_range;

struct xx_rarx29_state {
    uint8_t *window;
    size_t window_size;
    size_t window_pos;
    size_t history_size;

    uint8_t previous_lengths[XX_RAR29_TABLE_LENGTHS];
    /* Two table sets: a new set is built in the inactive one and becomes
     * active only when the whole table record was valid. */
    xx_rar29_huffman codes[2][XX_RAR29_CODE_COUNT];
    uint32_t main_packed[2][1u << XX_RAR29_MAIN_FAST_BITS];
    uint32_t distance_packed[2][1u << XX_RAR29_DISTANCE_FAST_BITS];
    unsigned active_codes;
    bool tables_valid;
    bool table_required;

    uint32_t old_distance[4];
    uint32_t last_distance;
    uint32_t last_length;
    uint32_t last_low_distance;
    uint32_t low_distance_repeats;

    xx_rar29_program *programs;
    size_t program_count;
    size_t program_capacity;
    size_t last_program;
    bool last_program_valid;

    CPpmd7 ppm_model;
    bool ppm_model_valid;
    uint8_t ppm_escape;
};

static void xx_rar29_clear_ppm_storage(CPpmd7 *model)
{
    if (model->Base) {
        /* Ppmd7_Alloc includes alignment bytes and one extra allocation unit. */
        xx_mem_zero(model->Base, (size_t)model->Size + (size_t)model->AlignOffset + PPMD7_UNIT_SIZE);
    }
}

static const uint16_t xx_rar29_length_base[XX_RAR29_LENGTH_SYMBOLS] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  10,  12,  14,  16,  20,
                                                                       24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224};

static const uint8_t xx_rar29_length_bits[XX_RAR29_LENGTH_SYMBOLS] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5};

static const uint32_t xx_rar29_distance_base[XX_RAR29_DISTANCE_SYMBOLS] = {
    0,      1,      2,      3,       4,       6,       8,       12,      16,      24,      32,      48,      64,      96,      128,
    192,    256,    384,    512,     768,     1024,    1536,    2048,    3072,    4096,    6144,    8192,    12288,   16384,   24576,
    32768,  49152,  65536,  98304,   131072,  196608,  262144,  327680,  393216,  458752,  524288,  589824,  655360,  720896,  786432,
    851968, 917504, 983040, 1048576, 1310720, 1572864, 1835008, 2097152, 2359296, 2621440, 2883584, 3145728, 3407872, 3670016, 3932160};

static const uint8_t xx_rar29_distance_bits[XX_RAR29_DISTANCE_SYMBOLS] = {0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,
                                                                          9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16,
                                                                          16, 16, 16, 16, 16, 16, 16, 16, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18};

static const uint8_t xx_rar29_short_base[8] = {0, 4, 8, 16, 32, 64, 128, 192};
static const uint8_t xx_rar29_short_bits[8] = {2, 2, 3, 4, 5, 6, 6, 6};

static inline bool xx_rar29_peek_bits(xx_rar29_bits *bits, unsigned count, uint32_t *value)
{
    size_t byte_pos = bits->bit >> 3;
    unsigned bit_offset = (unsigned)(bits->bit & 7u);

    if (byte_pos + 8 <= bits->size) {
        if (bit_offset + count <= 32u) {
            uint32_t w = xx_data_get_be32(&bits->data[byte_pos]);
            *value = (w << bit_offset) >> (32u - count);
            return true;
        }
        uint64_t w = xx_data_get_be64(&bits->data[byte_pos]);
        *value = (uint32_t)((w << bit_offset) >> (64u - count));
        return true;
    }
    if (!bits || !value || count > 32u) {
        if (bits) bits->error = XX_RARX_STATUS_CORRUPT;
        return false;
    }
    if (count == 0) {
        *value = 0;
        return true;
    }
    size_t end_bit = bits->bit + count;
    if (end_bit < bits->bit) {
        bits->error = XX_RARX_STATUS_LIMIT;
        return false;
    }
    size_t required_bytes = (end_bit + 7u) >> 3;
    if (required_bytes > bits->size) {
        bits->error = XX_RARX_STATUS_TRUNCATED;
        return false;
    }
    uint64_t w = 0;
    size_t rem = bits->size - byte_pos;
    for (size_t j = 0; j < rem && j < 8; ++j) {
        w |= (uint64_t)bits->data[byte_pos + j] << (56u - j * 8u);
    }
    *value = (uint32_t)((w << bit_offset) >> (64u - count));
    return true;
}

static inline bool xx_rar29_read_bits(xx_rar29_bits *bits, unsigned count, uint32_t *value)
{
    if (!xx_rar29_peek_bits(bits, count, value)) return false;
    bits->bit += count;
    return true;
}

static bool xx_rar29_align_byte(xx_rar29_bits *bits)
{
    size_t aligned;
    if (!bits) return false;
    if (bits->bit > SIZE_MAX - 7u) {
        bits->error = XX_RARX_STATUS_LIMIT;
        return false;
    }
    aligned = (bits->bit + 7u) & ~(size_t)7u;
    if ((aligned >> 3) > bits->size) {
        bits->error = XX_RARX_STATUS_TRUNCATED;
        return false;
    }
    bits->bit = aligned;
    return true;
}

static bool xx_rar29_build_huffman(xx_rar29_huffman *code, const uint8_t *lengths, size_t symbol_count, unsigned fast_bits)
{
    uint32_t next_code = 0;
    uint32_t used = 0;
    int32_t available = 1;
    unsigned length;
    size_t symbol;

    if (!code || !lengths || symbol_count == 0 || symbol_count > XX_RAR29_TABLE_LENGTHS) {
        return false;
    }
    xx_rt_memset(code->count, 0, sizeof(code->count));
    code->first_symbol[0] = 0;
    code->first_code[0] = 0;
    code->symbol_count = (uint16_t)symbol_count;
    code->fast_bits = (uint8_t)fast_bits;
    code->valid = false;

    for (symbol = 0; symbol < symbol_count; ++symbol) {
        if (lengths[symbol] > XX_RAR29_MAX_BITS) return false;
        if (lengths[symbol] != 0) {
            ++code->count[lengths[symbol]];
            ++used;
        }
    }
    /* RAR permits an unused auxiliary alphabet to contain only zero lengths.
     * Keep it invalid for decoding, but do not reject the surrounding table. */
    if (used == 0) {
        xx_rt_memset(code->fast_table, 0xff, ((size_t)1u << fast_bits) * sizeof(code->fast_table[0]));
        return true;
    }

    for (length = 1; length <= XX_RAR29_MAX_BITS; ++length) {
        available = available * 2 - (int32_t)code->count[length];
        if (available < 0) return false;
        next_code = (next_code + code->count[length - 1u]) << 1;
        code->first_code[length] = next_code;
        code->first_symbol[length] = (uint16_t)(code->first_symbol[length - 1u] + code->count[length - 1u]);
    }

    {
        uint16_t cursor[XX_RAR29_MAX_BITS + 1u];
        xx_rt_memcpy(cursor, code->first_symbol, sizeof(cursor));
        for (symbol = 0; symbol < symbol_count; ++symbol) {
            uint8_t bits = lengths[symbol];
            if (bits != 0) code->symbols[cursor[bits]++] = (uint16_t)symbol;
        }
    }

    /* Build the direct lookup table for codes of at most fast_bits bits.  The
     * code is canonical, so these codes fill the table from index 0 upwards
     * without gaps; everything after them belongs to longer or unassigned
     * codes and is marked invalid. */
    {
        uint32_t filled = 0;
        uint32_t table_size = 1u << fast_bits;
        for (length = 1; length <= fast_bits; ++length) {
            uint16_t cnt = code->count[length];
            uint32_t first = code->first_code[length];
            uint16_t fidx = code->first_symbol[length];
            unsigned shift = fast_bits - length;
            uint32_t step = 1u << shift;
            uint16_t k;

            for (k = 0; k < cnt; ++k) {
                uint32_t sym_code = first + (uint32_t)k;
                uint16_t sym = code->symbols[fidx + k];
                uint16_t cur_entry = (uint16_t)((sym << 4) | length);
                uint32_t base_pattern = sym_code << shift;
                uint32_t p = 0;

                if (step >= 4) {
                    uint32_t cur2 = ((uint32_t)cur_entry << 16) | cur_entry;
                    uint64_t cur4 = ((uint64_t)cur2 << 32) | cur2;
                    while (p + 4 <= step) {
                        xx_mem_store64(&code->fast_table[base_pattern + p], cur4);
                        p += 4;
                    }
                }
                while (p < step) {
                    code->fast_table[base_pattern + p] = cur_entry;
                    p++;
                }
                filled = base_pattern + step;
            }
        }
        if (filled < table_size) xx_rt_memset(&code->fast_table[filled], 0xff, (size_t)(table_size - filled) * sizeof(code->fast_table[0]));
    }

    code->valid = true;
    return true;
}

static bool xx_rar29_decode_symbol_slow(xx_rar29_bits *bits, const xx_rar29_huffman *code, uint32_t *symbol)
{
    uint32_t peek15;
    unsigned length;

    if (!xx_rar29_peek_bits(bits, XX_RAR29_MAX_BITS, &peek15)) {
        uint32_t current = 0;
        for (length = 1; length <= XX_RAR29_MAX_BITS; ++length) {
            uint32_t bit;
            uint32_t delta;
            if (!xx_rar29_read_bits(bits, 1, &bit)) return false;
            current = (current << 1) | bit;
            if (current < code->first_code[length]) continue;
            delta = current - code->first_code[length];
            if (delta < code->count[length]) {
                uint32_t index = (uint32_t)code->first_symbol[length] + delta;
                if (index >= code->symbol_count) {
                    bits->error = XX_RARX_STATUS_CORRUPT;
                    return false;
                }
                *symbol = code->symbols[index];
                return true;
            }
        }
        bits->error = XX_RARX_STATUS_CORRUPT;
        return false;
    }

    for (length = (unsigned)code->fast_bits + 1u; length <= XX_RAR29_MAX_BITS; ++length) {
        uint32_t cur = peek15 >> (XX_RAR29_MAX_BITS - length);
        if (cur < code->first_code[length]) continue;
        uint32_t delta = cur - code->first_code[length];
        if (delta < code->count[length]) {
            uint32_t index = (uint32_t)code->first_symbol[length] + delta;
            if (index >= code->symbol_count) {
                bits->error = XX_RARX_STATUS_CORRUPT;
                return false;
            }
            *symbol = code->symbols[index];
            bits->bit += length;
            return true;
        }
    }
    bits->error = XX_RARX_STATUS_CORRUPT;
    return false;
}

static inline bool xx_rar29_decode_symbol(xx_rar29_bits *bits, const xx_rar29_huffman *code, uint32_t *symbol)
{
    uint32_t peek;

    if (!bits || !code || !symbol || !code->valid) {
        if (bits) bits->error = XX_RARX_STATUS_CORRUPT;
        return false;
    }
    if (xx_rar29_peek_bits(bits, code->fast_bits, &peek)) {
        uint32_t entry = code->fast_table[peek];
        if (entry != XX_RAR29_FAST_INVALID) {
            *symbol = entry >> 4;
            bits->bit += (entry & 0x0Fu);
            return true;
        }
    }
    return xx_rar29_decode_symbol_slow(bits, code, symbol);
}

static inline uint32_t xx_rar29_pack_main(uint32_t symbol, uint32_t length)
{
    uint32_t base = 0;
    uint32_t extra_bits = 0;
    if (symbol >= 271u && symbol - 271u < XX_RAR29_LENGTH_SYMBOLS) {
        base = (uint32_t)xx_rar29_length_base[symbol - 271u] + 3u;
        extra_bits = xx_rar29_length_bits[symbol - 271u];
    } else if (symbol >= 263u && symbol < 271u) {
        base = (uint32_t)xx_rar29_short_base[symbol - 263u] + 1u;
        extra_bits = xx_rar29_short_bits[symbol - 263u];
    }
    return (symbol << XX_RAR29_PACKED_SYMBOL_SHIFT) | (base << 9) | (extra_bits << 6) | (length + extra_bits);
}

static inline uint32_t xx_rar29_pack_distance(uint32_t slot, uint32_t length)
{
    uint32_t direct_bits;
    if (slot >= XX_RAR29_DISTANCE_SYMBOLS) return XX_RAR29_PACKED_INVALID;
    direct_bits = xx_rar29_distance_bits[slot];
    if (slot > 9u) direct_bits -= 4u;
    return ((xx_rar29_distance_base[slot] + 1u) << 10) | (direct_bits << 6) | (length + direct_bits);
}

/* Builds one packed fast-loop table (see XX_RAR29_PACKED_INVALID for the
 * layout) of 1 << table_bits entries from a built code.  The codes of at most
 * table_bits bits fill the table from index 0 upwards in canonical order; the
 * rest of the table is invalid. */
static void xx_rar29_build_packed(const xx_rar29_huffman *code, uint32_t *table, unsigned table_bits, bool distance)
{
    uint32_t table_size = 1u << table_bits;
    uint32_t position = 0;
    unsigned length;

    if (code->valid) {
        for (length = 1; length <= table_bits; ++length) {
            uint32_t step = 1u << (table_bits - length);
            const uint16_t *symbols = &code->symbols[code->first_symbol[length]];
            uint32_t remaining = code->count[length];
            for (; remaining != 0; --remaining) {
                uint32_t symbol = *symbols++;
                uint32_t packed = distance ? xx_rar29_pack_distance(symbol, length) : xx_rar29_pack_main(symbol, length);
                uint32_t i;
                if (step > table_size - position) break; /* cannot happen for a prefix code */
                for (i = 0; i < step; ++i) table[position + i] = packed;
                position += step;
            }
        }
    }
    for (; position < table_size; ++position) table[position] = XX_RAR29_PACKED_INVALID;
}

static xx_rarx_status_t xx_rar29_read_tables(xx_rarx29_state *state, xx_rar29_bits *bits, bool *ppm_mode)
{
    uint8_t level_lengths[XX_RAR29_LEVEL_SYMBOLS];
    uint8_t lengths[XX_RAR29_TABLE_LENGTHS];
    xx_rar29_huffman level_code;
    xx_rar29_huffman *next_codes;
    uint32_t value;
    size_t i;

    if (!state || !bits || !ppm_mode) return XX_RARX_STATUS_INVALID_ARGUMENT;
    *ppm_mode = false;
    if (!xx_rar29_align_byte(bits)) return bits->error;
    if ((bits->bit >> 3) >= bits->size) return XX_RARX_STATUS_TRUNCATED;
    if ((bits->data[bits->bit >> 3] & 0x80u) != 0) {
        /* PPMd consumes this complete byte itself, including the mode bit. */
        *ppm_mode = true;
        state->table_required = false;
        return XX_RARX_STATUS_OK;
    }
    if (!xx_rar29_read_bits(bits, 1, &value)) return bits->error;
    if (!xx_rar29_read_bits(bits, 1, &value)) return bits->error;
    if (value != 0) xx_rt_memcpy(lengths, state->previous_lengths, sizeof(lengths));
    else xx_rt_memset(lengths, 0, sizeof(lengths));

    xx_rt_memset(level_lengths, 0, sizeof(level_lengths));
    for (i = 0; i < XX_RAR29_LEVEL_SYMBOLS;) {
        uint32_t encoded;
        if (!xx_rar29_read_bits(bits, 4, &encoded)) return bits->error;
        if (encoded == 15u) {
            uint32_t zeros;
            if (!xx_rar29_read_bits(bits, 4, &zeros)) return bits->error;
            if (zeros != 0) {
                size_t run = (size_t)zeros + 2u;
                if (run > XX_RAR29_LEVEL_SYMBOLS - i) return XX_RARX_STATUS_CORRUPT;
                xx_rt_memset(level_lengths + i, 0, run);
                i += run;
                continue;
            }
        }
        level_lengths[i++] = (uint8_t)encoded;
    }
    if (!xx_rar29_build_huffman(&level_code, level_lengths, XX_RAR29_LEVEL_SYMBOLS, XX_RAR29_LEVEL_FAST_BITS) || !level_code.valid) {
        return XX_RARX_STATUS_CORRUPT;
    }

    for (i = 0; i < XX_RAR29_TABLE_LENGTHS;) {
        uint32_t symbol;
        if (!xx_rar29_decode_symbol(bits, &level_code, &symbol)) return bits->error;
        if (symbol < 16u) {
            lengths[i] = (uint8_t)((lengths[i] + symbol) & 15u);
            ++i;
        } else if (symbol == 16u || symbol == 17u) {
            uint32_t extra;
            size_t run;
            if (i == 0) return XX_RARX_STATUS_CORRUPT;
            if (!xx_rar29_read_bits(bits, symbol == 16u ? 3u : 7u, &extra)) return bits->error;
            run = (size_t)extra + (symbol == 16u ? 3u : 11u);
            if (run > XX_RAR29_TABLE_LENGTHS - i) return XX_RARX_STATUS_CORRUPT;
            xx_rt_memset(lengths + i, lengths[i - 1u], run);
            i += run;
        } else if (symbol == 18u || symbol == 19u) {
            uint32_t extra;
            size_t run;
            if (!xx_rar29_read_bits(bits, symbol == 18u ? 3u : 7u, &extra)) return bits->error;
            run = (size_t)extra + (symbol == 18u ? 3u : 11u);
            if (run > XX_RAR29_TABLE_LENGTHS - i) return XX_RARX_STATUS_CORRUPT;
            xx_rt_memset(lengths + i, 0, run);
            i += run;
        } else {
            return XX_RARX_STATUS_CORRUPT;
        }
    }

    /* Build into the inactive set, so a rejected table record leaves the
     * tables of the state exactly as they were. */
    next_codes = state->codes[(state->active_codes ^ 1u) & 1u];
    if (!xx_rar29_build_huffman(&next_codes[XX_RAR29_CODE_MAIN], lengths, XX_RAR29_MAIN_SYMBOLS, XX_RAR29_MAIN_FAST_BITS) ||
        !xx_rar29_build_huffman(&next_codes[XX_RAR29_CODE_DISTANCE], lengths + XX_RAR29_MAIN_SYMBOLS, XX_RAR29_DISTANCE_SYMBOLS, XX_RAR29_DISTANCE_FAST_BITS) ||
        !xx_rar29_build_huffman(&next_codes[XX_RAR29_CODE_LOW], lengths + XX_RAR29_MAIN_SYMBOLS + XX_RAR29_DISTANCE_SYMBOLS, XX_RAR29_LOW_SYMBOLS,
                                XX_RAR29_LOW_FAST_BITS) ||
        !xx_rar29_build_huffman(&next_codes[XX_RAR29_CODE_LENGTH], lengths + XX_RAR29_MAIN_SYMBOLS + XX_RAR29_DISTANCE_SYMBOLS + XX_RAR29_LOW_SYMBOLS,
                                XX_RAR29_LENGTH_SYMBOLS, XX_RAR29_LENGTH_FAST_BITS) ||
        !next_codes[XX_RAR29_CODE_MAIN].valid) {
        return XX_RARX_STATUS_CORRUPT;
    }

    xx_rt_memcpy(state->previous_lengths, lengths, sizeof(lengths));
    {
        unsigned next_set = (state->active_codes ^ 1u) & 1u;
        xx_rar29_build_packed(&next_codes[XX_RAR29_CODE_MAIN], state->main_packed[next_set], XX_RAR29_MAIN_FAST_BITS, false);
        xx_rar29_build_packed(&next_codes[XX_RAR29_CODE_DISTANCE], state->distance_packed[next_set], XX_RAR29_DISTANCE_FAST_BITS, true);
    }
    state->active_codes = (state->active_codes ^ 1u) & 1u;
    state->tables_valid = true;
    state->table_required = false;
    return XX_RARX_STATUS_OK;
}

static bool xx_rar29_ppm_read_byte(xx_rar29_ppm_range *coder, uint8_t *value)
{
    xx_rar29_bits *input;
    size_t position;
    if (!coder || !coder->input || !value) return false;
    input = coder->input;
    if ((input->bit & 7u) != 0) {
        input->error = XX_RARX_STATUS_CORRUPT;
        return false;
    }
    position = input->bit >> 3;
    if (position >= input->size) {
        input->error = XX_RARX_STATUS_TRUNCATED;
        return false;
    }
    if (input->bit > SIZE_MAX - 8u) {
        input->error = XX_RARX_STATUS_LIMIT;
        return false;
    }
    *value = input->data[position];
    input->bit += 8u;
    return true;
}

static bool xx_rar29_ppm_normalize(xx_rar29_ppm_range *coder)
{
    const uint32_t top = 1u << 24;
    const uint32_t bottom = 1u << 15;
    for (;;) {
        uint8_t byte;
        if ((coder->low ^ (coder->low + coder->range)) >= top) {
            if (coder->range >= bottom) break;
            coder->range = (0u - coder->low) & (bottom - 1u);
        }
        if (!xx_rar29_ppm_read_byte(coder, &byte)) return false;
        coder->code = (coder->code << 8) | byte;
        coder->range <<= 8;
        coder->low <<= 8;
    }
    return true;
}

static bool xx_rar29_ppm_threshold(xx_rar29_ppm_range *coder, uint32_t total, uint32_t *count)
{
    uint32_t unit;
    if (!coder || !count || total == 0) return false;
    unit = coder->range / total;
    if (unit == 0) {
        coder->input->error = XX_RARX_STATUS_CORRUPT;
        return false;
    }
    coder->range = unit;
    *count = (coder->code - coder->low) / unit;
    if (*count >= total) {
        coder->input->error = XX_RARX_STATUS_CORRUPT;
        return false;
    }
    return true;
}

static void xx_rar29_ppm_narrow(xx_rar29_ppm_range *coder, uint32_t start, uint32_t size)
{
    coder->low += start * coder->range;
    coder->range *= size;
}

static xx_rarx_status_t xx_rar29_ppm_begin(xx_rarx29_state *state, xx_rar29_bits *input, xx_rar29_ppm_range *coder, size_t allocation_limit)
{
    uint8_t header;
    uint8_t byte;
    uint32_t memory_mb = 0;
    unsigned order = 0;
    unsigned i;

    if (!state || !input || !coder) return XX_RARX_STATUS_INVALID_ARGUMENT;
    xx_rt_memset(coder, 0, sizeof(*coder));
    coder->input = input;
    if (!xx_rar29_ppm_read_byte(coder, &header)) return input->error;
    if ((header & 0x80u) == 0) return XX_RARX_STATUS_CORRUPT;

    if ((header & 0x20u) != 0) {
        if (!xx_rar29_ppm_read_byte(coder, &byte)) return input->error;
        memory_mb = (uint32_t)byte + 1u;
        order = (unsigned)(header & 0x1fu) + 1u;
        if (order > 16u) order = 16u + (order - 16u) * 3u;
        if (order < 2u) return XX_RARX_STATUS_CORRUPT;
        if (order > XX_RAR29_PPM_MAX_ORDER) return XX_RARX_STATUS_UNSUPPORTED_VERSION;
    } else if (!state->ppm_model_valid) {
        return XX_RARX_STATUS_CORRUPT;
    } else if ((size_t)state->ppm_model.Size > allocation_limit) {
        return XX_RARX_STATUS_LIMIT;
    }

    if ((header & 0x40u) != 0) {
        if (!xx_rar29_ppm_read_byte(coder, &state->ppm_escape)) return input->error;
    }

    coder->range = UINT32_MAX;
    coder->low = 0;
    coder->code = 0;
    for (i = 0; i < 4u; ++i) {
        if (!xx_rar29_ppm_read_byte(coder, &byte)) return input->error;
        coder->code = (coder->code << 8) | byte;
    }
    if (coder->code == UINT32_MAX) return XX_RARX_STATUS_CORRUPT;

    if ((header & 0x20u) != 0) {
        size_t memory_size;
        if (memory_mb == 0 || memory_mb > XX_PPMD7_MAX_MEM_MB) return XX_RARX_STATUS_LIMIT;
        memory_size = (size_t)memory_mb * (1024u * 1024u);
        if (memory_size > allocation_limit || memory_size > UINT32_MAX) return XX_RARX_STATUS_LIMIT;
        state->ppm_model_valid = false;
        xx_rar29_clear_ppm_storage(&state->ppm_model);
        if (!Ppmd7_Alloc(&state->ppm_model, (uint32_t)memory_size)) return XX_RARX_STATUS_NO_MEMORY;
        Ppmd7_Init(&state->ppm_model, order);
        state->ppm_model_valid = true;
    }
    state->table_required = false;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rar29_ppm_decode_symbol(xx_rarx29_state *state, xx_rar29_ppm_range *coder, uint8_t *output)
{
    CPpmd7 *model;
    size_t char_mask[256u / sizeof(size_t)];
    CPpmd_State *symbol_state = NULL;
    uint32_t count;
    uint32_t high_count;
    unsigned i;

    if (!state || !coder || !output || !state->ppm_model_valid) return XX_RARX_STATUS_CORRUPT;
    model = &state->ppm_model;

    if (model->MinContext->NumStats != 1u) {
        CPpmd_State *states = Ppmd7_GetStats(model, model->MinContext);
        unsigned number = model->MinContext->NumStats;
        if (!xx_rar29_ppm_threshold(coder, model->MinContext->SummFreq, &count)) return coder->input->error;
        high_count = 0;
        for (i = 0; i < number; ++i) {
            high_count += states[i].Freq;
            if (count < high_count) {
                symbol_state = states + i;
                xx_rar29_ppm_narrow(coder, high_count - symbol_state->Freq, symbol_state->Freq);
                model->FoundState = symbol_state;
                *output = symbol_state->Symbol;
                if (i == 0) Ppmd7_Update1_0(model);
                else {
                    model->PrevSuccess = 0;
                    Ppmd7_Update1(model);
                }
                if (!xx_rar29_ppm_normalize(coder)) return coder->input->error;
                return XX_RARX_STATUS_OK;
            }
        }
        xx_rar29_ppm_narrow(coder, high_count, model->MinContext->SummFreq - high_count);
        model->HiBitsFlag = model->HB2Flag[model->FoundState->Symbol];
        PPMD_SetAllBitsIn256Bytes(char_mask);
        for (i = 0; i < number; ++i) ((int8_t *)char_mask)[states[i].Symbol] = 0;
        model->PrevSuccess = 0;
    } else {
        uint16_t *probability = Ppmd7_GetBinSumm(model);
        CPpmd_State *one = Ppmd7Context_OneState(model->MinContext);
        if (!xx_rar29_ppm_threshold(coder, PPMD_BIN_SCALE, &count)) return coder->input->error;
        if (count < *probability) {
            xx_rar29_ppm_narrow(coder, 0, *probability);
            *probability = (uint16_t)PPMD_UPDATE_PROB_0(*probability);
            model->FoundState = one;
            *output = one->Symbol;
            Ppmd7_UpdateBin(model);
            if (!xx_rar29_ppm_normalize(coder)) return coder->input->error;
            return XX_RARX_STATUS_OK;
        }
        xx_rar29_ppm_narrow(coder, *probability, PPMD_BIN_SCALE - *probability);
        *probability = (uint16_t)PPMD_UPDATE_PROB_1(*probability);
        model->InitEsc = PPMD7_kExpEscape[*probability >> 10];
        PPMD_SetAllBitsIn256Bytes(char_mask);
        ((int8_t *)char_mask)[one->Symbol] = 0;
        model->PrevSuccess = 0;
    }

    for (;;) {
        CPpmd_State *available[256];
        CPpmd_State *states;
        CPpmd_See *see;
        uint32_t frequency_sum;
        unsigned available_count = 0;
        unsigned masked_count = model->MinContext->NumStats;
        unsigned number;

        if (!xx_rar29_ppm_normalize(coder)) return coder->input->error;
        do {
            ++model->OrderFall;
            if (model->MinContext->Suffix == 0) return XX_RARX_STATUS_CORRUPT;
            model->MinContext = Ppmd7_GetContext(model, model->MinContext->Suffix);
        } while (model->MinContext->NumStats == masked_count);

        states = Ppmd7_GetStats(model, model->MinContext);
        number = model->MinContext->NumStats;
        high_count = 0;
        for (i = 0; i < number; ++i) {
            if (((int8_t *)char_mask)[states[i].Symbol] != 0) {
                available[available_count++] = states + i;
                high_count += states[i].Freq;
            }
        }
        if (available_count != number - masked_count) return XX_RARX_STATUS_CORRUPT;

        see = Ppmd7_MakeEscFreq(model, masked_count, &frequency_sum);
        frequency_sum += high_count;
        if (!xx_rar29_ppm_threshold(coder, frequency_sum, &count)) return coder->input->error;
        if (count < high_count) {
            high_count = 0;
            for (i = 0; i < available_count; ++i) {
                symbol_state = available[i];
                high_count += symbol_state->Freq;
                if (count < high_count) break;
            }
            if (i == available_count) return XX_RARX_STATUS_CORRUPT;
            xx_rar29_ppm_narrow(coder, high_count - symbol_state->Freq, symbol_state->Freq);
            Ppmd_See_Update(see);
            model->FoundState = symbol_state;
            *output = symbol_state->Symbol;
            Ppmd7_Update2(model);
            if (!xx_rar29_ppm_normalize(coder)) return coder->input->error;
            return XX_RARX_STATUS_OK;
        }

        xx_rar29_ppm_narrow(coder, high_count, frequency_sum - high_count);
        see->Summ = (uint16_t)(see->Summ + frequency_sum);
        for (i = 0; i < available_count; ++i) ((int8_t *)char_mask)[available[i]->Symbol] = 0;
    }
}

static bool xx_rar29_vm_number(xx_rar29_bits *bits, uint32_t *number)
{
    uint32_t prefix;
    uint32_t value;
    if (!xx_rar29_read_bits(bits, 2, &prefix)) return false;
    switch (prefix) {
        case 0: return xx_rar29_read_bits(bits, 4, number);
        case 1:
            if (!xx_rar29_read_bits(bits, 8, &value)) return false;
            if (value >= 16u) {
                *number = value;
                return true;
            }
            if (!xx_rar29_read_bits(bits, 4, number)) return false;
            *number = 0xffffff00u | (value << 4) | *number;
            return true;
        case 2: return xx_rar29_read_bits(bits, 16, number);
        default: {
            uint32_t high;
            uint32_t low;
            if (!xx_rar29_read_bits(bits, 16, &high) || !xx_rar29_read_bits(bits, 16, &low)) return false;
            *number = (high << 16) | low;
            return true;
        }
    }
}

static bool xx_rar29_identify_filter(const uint8_t *code, size_t size, xx_rar29_filter_type *type)
{
    uint32_t crc;
    uint8_t checksum = 0;
    size_t i;
    if (!code || !type || size == 0) return false;
    for (i = 0; i < size; ++i) checksum ^= code[i];
    if (checksum != 0) return false;
    crc = xx_crc32_calc(0U, code, size);
    if (size == 53u && crc == 0xad576887u) *type = XX_RAR29_FILTER_E8;
    else if (size == 57u && crc == 0x3cd7e57eu) *type = XX_RAR29_FILTER_E8E9;
    else if (size == 120u && crc == 0x3769893fu) *type = XX_RAR29_FILTER_ITANIUM;
    else if (size == 29u && crc == 0x0e06077du) *type = XX_RAR29_FILTER_DELTA;
    else if (size == 149u && crc == 0x1c2c5dc8u) *type = XX_RAR29_FILTER_RGB;
    else if (size == 216u && crc == 0xbc85e701u) *type = XX_RAR29_FILTER_AUDIO;
    else return false;
    return true;
}

static xx_rarx_status_t xx_rar29_reserve_program(xx_rarx29_state *state)
{
    xx_rar29_program *grown;
    size_t capacity;
    if (state->program_count < state->program_capacity) return XX_RARX_STATUS_OK;
    if (state->program_count >= XX_RAR29_MAX_PROGRAMS) return XX_RARX_STATUS_LIMIT;
    capacity = state->program_capacity ? state->program_capacity * 2u : 8u;
    if (capacity > XX_RAR29_MAX_PROGRAMS) capacity = XX_RAR29_MAX_PROGRAMS;
    if (capacity > SIZE_MAX / sizeof(*grown)) return XX_RARX_STATUS_LIMIT;
    grown = (xx_rar29_program *)xx_rarx_clear_resize(state->programs, state->program_capacity * sizeof(*grown), capacity * sizeof(*grown));
    if (!grown) return XX_RARX_STATUS_NO_MEMORY;
    state->programs = grown;
    state->program_capacity = capacity;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rar29_append_filter(xx_rar29_filter_list *list, const xx_rar29_filter *filter, size_t output_size)
{
    xx_rar29_filter *grown;
    size_t capacity;
    /* Filters run after the whole member is decoded, so the bound covers all
     * filters of the member and scales with its size (see xx_rarx50.c). */
    size_t limit = XX_RAR29_MAX_FILTERS + output_size / XX_RAR29_FILTER_BYTES_PER_SLOT;
    if (list->count >= limit) return XX_RARX_STATUS_LIMIT;
    if (list->count == list->capacity) {
        capacity = list->capacity ? list->capacity * 2u : 8u;
        if (capacity > limit) capacity = limit;
        if (capacity > SIZE_MAX / sizeof(*grown)) return XX_RARX_STATUS_LIMIT;
        grown = (xx_rar29_filter *)xx_rarx_clear_resize(list->items, list->capacity * sizeof(*grown), capacity * sizeof(*grown));
        if (!grown) return XX_RARX_STATUS_NO_MEMORY;
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count++] = *filter;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rar29_read_filter(xx_rarx29_state *state, xx_rar29_bits *stream, size_t produced, size_t output_size, xx_rar29_filter_list *filters)
{
    uint8_t *payload = NULL;
    uint8_t *program_bytes = NULL;
    xx_rar29_bits vm;
    xx_rar29_filter filter;
    xx_rar29_program *program;
    xx_rarx_status_t status = XX_RARX_STATUS_CORRUPT;
    uint32_t value;
    uint32_t first;
    size_t payload_size;
    size_t code_size = 0;
    size_t selected;
    bool new_program;
    size_t i;

    xx_rt_memset(&filter, 0, sizeof(filter));
    if (!xx_rar29_read_bits(stream, 8, &first)) return stream->error;
    payload_size = (size_t)(first & 7u) + 1u;
    if (payload_size == 7u) {
        if (!xx_rar29_read_bits(stream, 8, &value)) return stream->error;
        payload_size = (size_t)value + 7u;
    } else if (payload_size == 8u) {
        if (!xx_rar29_read_bits(stream, 16, &value)) return stream->error;
        payload_size = (size_t)value;
    }
    if (payload_size == 0 || payload_size > 65535u) return XX_RARX_STATUS_CORRUPT;
    payload = (uint8_t *)xx_mem_alloc(payload_size);
    if (!payload) return XX_RARX_STATUS_NO_MEMORY;
    for (i = 0; i < payload_size; ++i) {
        if (!xx_rar29_read_bits(stream, 8, &value)) {
            status = stream->error;
            goto done;
        }
        payload[i] = (uint8_t)value;
    }
    xx_rt_memset(&vm, 0, sizeof(vm));
    vm.data = payload;
    vm.size = payload_size;
    vm.error = XX_RARX_STATUS_OK;

    if ((first & 0x80u) != 0) {
        if (!xx_rar29_vm_number(&vm, &value)) {
            status = vm.error;
            goto done;
        }
        if (value == 0) {
            state->program_count = 0;
            state->last_program_valid = false;
            selected = 0;
        } else {
            selected = (size_t)value - 1u;
        }
    } else if (state->last_program_valid) {
        selected = state->last_program;
    } else {
        selected = 0;
    }
    if (selected > state->program_count) goto done;
    new_program = selected == state->program_count;

    if (!xx_rar29_vm_number(&vm, &value)) {
        status = vm.error;
        goto done;
    }
    if ((first & 0x40u) != 0) {
        if (value > UINT32_MAX - 258u) goto done;
        value += 258u;
    }
    if ((size_t)value > SIZE_MAX - produced) {
        status = XX_RARX_STATUS_LIMIT;
        goto done;
    }
    filter.start = produced + (size_t)value;

    if ((first & 0x20u) != 0) {
        if (!xx_rar29_vm_number(&vm, &value)) {
            status = vm.error;
            goto done;
        }
        filter.length = (size_t)value;
    } else if (!new_program) {
        filter.length = state->programs[selected].previous_length;
    } else {
        goto done;
    }
    if (filter.length == 0 || filter.length > XX_RAR29_VM_WORK_SIZE || filter.start > output_size || filter.length > output_size - filter.start) {
        goto done;
    }

    if ((first & 0x10u) != 0) {
        uint32_t mask;
        if (!xx_rar29_read_bits(&vm, 7, &mask)) {
            status = vm.error;
            goto done;
        }
        for (i = 0; i < 7u; ++i) {
            if ((mask & (1u << i)) != 0) {
                if (!xx_rar29_vm_number(&vm, &filter.registers[i])) {
                    status = vm.error;
                    goto done;
                }
            }
        }
    }

    if (new_program) {
        xx_rar29_filter_type type;
        if (!xx_rar29_vm_number(&vm, &value)) {
            status = vm.error;
            goto done;
        }
        code_size = (size_t)value;
        if (code_size == 0 || code_size >= 65536u) goto done;
        program_bytes = (uint8_t *)xx_mem_alloc(code_size);
        if (!program_bytes) {
            status = XX_RARX_STATUS_NO_MEMORY;
            goto done;
        }
        for (i = 0; i < code_size; ++i) {
            if (!xx_rar29_read_bits(&vm, 8, &value)) {
                status = vm.error;
                goto done;
            }
            program_bytes[i] = (uint8_t)value;
        }
        if (!xx_rar29_identify_filter(program_bytes, code_size, &type)) {
            status = XX_RARX_STATUS_UNSUPPORTED_FILTER;
            goto done;
        }
        status = xx_rar29_reserve_program(state);
        if (status != XX_RARX_STATUS_OK) goto done;
        xx_rt_memset(&state->programs[selected], 0, sizeof(state->programs[selected]));
        state->programs[selected].type = type;
        ++state->program_count;
    }
    program = &state->programs[selected];
    filter.type = program->type;

    if ((first & 0x08u) != 0) {
        uint32_t global_size;
        if (!xx_rar29_vm_number(&vm, &global_size)) {
            status = vm.error;
            goto done;
        }
        if (global_size > 0x1fc0u) {
            status = XX_RARX_STATUS_LIMIT;
            goto done;
        }
        for (i = 0; i < (size_t)global_size; ++i) {
            if (!xx_rar29_read_bits(&vm, 8, &value)) {
                status = vm.error;
                goto done;
            }
        }
        if (global_size != 0) {
            status = XX_RARX_STATUS_UNSUPPORTED_FILTER;
            goto done;
        }
    }

    program->previous_length = (uint32_t)filter.length;
    ++program->executions;
    state->last_program = selected;
    state->last_program_valid = true;
    status = xx_rar29_append_filter(filters, &filter, output_size);

done:
    xx_rarx_clear_free(program_bytes, code_size);
    xx_rarx_clear_free(payload, payload_size);
    xx_mem_zero(&filter, sizeof(filter));
    xx_mem_zero(&vm, sizeof(vm));
    return status;
}

/* Rewrites the 32-bit operand at data + pos (the byte after the opcode). */
static inline void xx_rar29_filter_e8_operand(uint8_t *data, size_t pos, size_t file_offset)
{
    const uint32_t file_span = 0x1000000u;
    uint32_t address = xx_data_get_le32(data + pos);
    /* RAR3 uses the full 32-bit stream position here; unlike RAR5 it is
     * not reduced modulo the 16 MiB address span, which only matters
     * beyond the first 16 MiB of a member. */
    uint32_t offset = (uint32_t)file_offset + (uint32_t)pos;
    if ((address & 0x80000000u) != 0) {
        if (((address + offset) & 0x80000000u) == 0) xx_data_set_le32(data + pos, address + file_span);
    } else if (address < file_span) {
        xx_data_set_le32(data + pos, address - offset);
    }
}

static void xx_rar29_filter_e8(uint8_t *data, size_t size, size_t file_offset, bool include_e9)
{
    size_t pos = 0;
    size_t limit; /* opcodes are looked for at positions below this */
    if (size < 5u) return;
    limit = size - 4u;
#if defined(XX_RAR29_USE_SSE2)
    {
        /* 0xe9 & 0xfe == 0xe8, so one compare finds both opcodes. */
        const __m128i fold = _mm_set1_epi8(include_e9 ? (char)-2 : (char)-1);
        const __m128i opcode_e8 = _mm_set1_epi8((char)-24);
        while (pos + 16u <= limit) {
            __m128i chunk = _mm_loadu_si128((const __m128i *)(const void *)(data + pos));
            unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_and_si128(chunk, fold), opcode_e8));
            if (mask == 0) {
                pos += 16u;
                continue;
            }
            {
#if defined(_MSC_VER)
                unsigned long first;
                _BitScanForward(&first, (unsigned long)mask);
#else
                unsigned first = (unsigned)__builtin_ctz(mask);
#endif
                pos += (size_t)first + 1u;
            }
            /* The operand is skipped, so the scan restarts right after it. */
            xx_rar29_filter_e8_operand(data, pos, file_offset);
            pos += 4u;
        }
    }
#endif
    while (pos < limit) {
        uint8_t opcode = data[pos++];
        if (opcode == 0xe8u || (include_e9 && opcode == 0xe9u)) {
            xx_rar29_filter_e8_operand(data, pos, file_offset);
            pos += 4u;
        }
    }
}

static xx_rarx_status_t xx_rar29_filter_delta(uint8_t *data, size_t size, uint32_t channels)
{
    uint8_t *result;
    size_t source = 0;
    size_t channel;
    if (channels == 0 || channels > 1024u || size > XX_RAR29_VM_HALF_SIZE) return XX_RARX_STATUS_CORRUPT;
    if (channels == 1) {
        uint8_t previous = 0;
        for (size_t i = 0; i < size; ++i) {
            previous = (uint8_t)(previous - data[i]);
            data[i] = previous;
        }
        return XX_RARX_STATUS_OK;
    }
    result = (uint8_t *)xx_mem_alloc(size ? size : 1u);
    if (!result) return XX_RARX_STATUS_NO_MEMORY;
    for (channel = 0; channel < channels; ++channel) {
        uint8_t previous = 0;
        size_t destination;
        for (destination = channel; destination < size; destination += channels) {
            previous = (uint8_t)(previous - data[source++]);
            result[destination] = previous;
        }
    }
    xx_rt_memcpy(data, result, size);
    xx_rarx_clear_free(result, size ? size : 1u);
    return XX_RARX_STATUS_OK;
}

static uint32_t xx_rar29_get_field(const uint8_t *data, size_t bit, unsigned count)
{
    uint32_t value = 0;
    unsigned i;
    for (i = 0; i < count; ++i) value |= (uint32_t)((data[(bit + i) >> 3] >> ((bit + i) & 7u)) & 1u) << i;
    return value;
}

static void xx_rar29_set_field(uint8_t *data, size_t bit, unsigned count, uint32_t value)
{
    unsigned i;
    for (i = 0; i < count; ++i) {
        uint8_t mask = (uint8_t)(1u << ((bit + i) & 7u));
        uint8_t *target = data + ((bit + i) >> 3);
        if ((value & (1u << i)) != 0) *target |= mask;
        else *target &= (uint8_t)~mask;
    }
}

static void xx_rar29_filter_itanium(uint8_t *data, size_t size, size_t file_offset)
{
    static const uint8_t slot_masks[16] = {4, 4, 6, 6, 0, 0, 7, 7, 4, 4, 0, 0, 4, 4, 0, 0};
    size_t pos = 0;
    uint32_t bundle = (uint32_t)(file_offset >> 4);
    while (pos + 21u < size) {
        uint8_t format = (uint8_t)(data[pos] & 0x1fu);
        if (format >= 0x10u) {
            uint8_t mask = slot_masks[format - 0x10u];
            unsigned slot;
            for (slot = 0; slot < 3u; ++slot) {
                size_t start = (size_t)slot * 41u + 5u;
                if ((mask & (1u << slot)) != 0 && xx_rar29_get_field(data + pos, start + 37u, 4u) == 5u) {
                    uint32_t target = xx_rar29_get_field(data + pos, start + 13u, 20u);
                    target = (target - bundle) & 0xfffffu;
                    xx_rar29_set_field(data + pos, start + 13u, 20u, target);
                }
            }
        }
        pos += 16u;
        ++bundle;
    }
}

static int xx_rar29_abs_int(int value)
{
    return value < 0 ? -value : value;
}

static xx_rarx_status_t xx_rar29_filter_rgb(uint8_t *data, size_t size, uint32_t width_parameter, uint32_t red_position)
{
    uint8_t *result;
    size_t width;
    size_t source = 0;
    size_t channel;
    if (width_parameter < 3u || red_position > 2u || size > XX_RAR29_VM_HALF_SIZE) return XX_RARX_STATUS_CORRUPT;
    width = (size_t)width_parameter - 3u;
    result = (uint8_t *)xx_mem_calloc(size ? size : 1u, 1u);
    if (!result) return XX_RARX_STATUS_NO_MEMORY;

    for (channel = 0; channel < 3u; ++channel) {
        uint8_t previous = 0;
        size_t destination;
        for (destination = channel; destination < size; destination += 3u) {
            int predicted = previous;
            if (destination >= width + 3u && width <= destination) {
                int upper = result[destination - width];
                int upper_left = result[destination - width - 3u];
                int mixed = (int)previous + upper - upper_left;
                int left_error = xx_rar29_abs_int(mixed - (int)previous);
                int upper_error = xx_rar29_abs_int(mixed - upper);
                int corner_error = xx_rar29_abs_int(mixed - upper_left);
                if (left_error <= upper_error && left_error <= corner_error) predicted = previous;
                else if (upper_error <= corner_error) predicted = upper;
                else predicted = upper_left;
            }
            previous = (uint8_t)(predicted - data[source++]);
            result[destination] = previous;
        }
    }
    for (source = red_position; source + 2u < size; source += 3u) {
        uint8_t green = result[source + 1u];
        result[source] = (uint8_t)(result[source] + green);
        result[source + 2u] = (uint8_t)(result[source + 2u] + green);
    }
    xx_rt_memcpy(data, result, size);
    xx_rarx_clear_free(result, size ? size : 1u);
    return XX_RARX_STATUS_OK;
}

static int xx_rar29_signed_byte(uint8_t value)
{
    return value < 128u ? (int)value : (int)value - 256;
}

static xx_rarx_status_t xx_rar29_filter_audio(uint8_t *data, size_t size, uint32_t channels)
{
    uint8_t *result;
    size_t source = 0;
    size_t channel;
    if (channels == 0 || channels > 1024u || size > XX_RAR29_VM_HALF_SIZE) return XX_RARX_STATUS_CORRUPT;
    result = (uint8_t *)xx_mem_calloc(size ? size : 1u, 1u);
    if (!result) return XX_RARX_STATUS_NO_MEMORY;

    for (channel = 0; channel < channels; ++channel) {
        int previous_byte = 0;
        int previous_delta = 0;
        int d1 = 0, d2 = 0, d3 = 0;
        int k1 = 0, k2 = 0, k3 = 0;
        uint32_t differences[7] = {0, 0, 0, 0, 0, 0, 0};
        size_t byte_count = 0;
        size_t position;
        for (position = channel; position < size; position += channels) {
            int predicted;
            int current;
            int residual;
            int delta;
            unsigned best;
            unsigned i;

            d3 = d2;
            d2 = previous_delta - d1;
            d1 = previous_delta;
            predicted = (8 * previous_byte + k1 * d1 + k2 * d2 + k3 * d3) >> 3;
            residual = xx_rar29_signed_byte(data[source]);
            current = (predicted - data[source]) & 0xff;
            ++source;
            result[position] = (uint8_t)current;
            previous_delta = xx_rar29_signed_byte((uint8_t)(current - previous_byte));
            previous_byte = current;

            delta = residual * 8;
            differences[0] += (uint32_t)xx_rar29_abs_int(delta);
            differences[1] += (uint32_t)xx_rar29_abs_int(delta - d1);
            differences[2] += (uint32_t)xx_rar29_abs_int(delta + d1);
            differences[3] += (uint32_t)xx_rar29_abs_int(delta - d2);
            differences[4] += (uint32_t)xx_rar29_abs_int(delta + d2);
            differences[5] += (uint32_t)xx_rar29_abs_int(delta - d3);
            differences[6] += (uint32_t)xx_rar29_abs_int(delta + d3);

            if ((byte_count & 31u) == 0) {
                best = 0;
                for (i = 1; i < 7u; ++i)
                    if (differences[i] < differences[best]) best = i;
                if (best == 1u && k1 >= -16) --k1;
                else if (best == 2u && k1 < 16) ++k1;
                else if (best == 3u && k2 >= -16) --k2;
                else if (best == 4u && k2 < 16) ++k2;
                else if (best == 5u && k3 >= -16) --k3;
                else if (best == 6u && k3 < 16) ++k3;
                xx_rt_memset(differences, 0, sizeof(differences));
            }
            ++byte_count;
        }
    }
    xx_rt_memcpy(data, result, size);
    xx_rarx_clear_free(result, size ? size : 1u);
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rar29_apply_filters(uint8_t *destination, size_t destination_size, const xx_rar29_filter_list *filters)
{
    size_t i;
    for (i = 0; i < filters->count; ++i) {
        const xx_rar29_filter *filter = &filters->items[i];
        uint8_t *data;
        if (filter->start > destination_size || filter->length > destination_size - filter->start) return XX_RARX_STATUS_CORRUPT;
        data = destination + filter->start;
        switch (filter->type) {
            case XX_RAR29_FILTER_E8: xx_rar29_filter_e8(data, filter->length, filter->start, false); break;
            case XX_RAR29_FILTER_E8E9: xx_rar29_filter_e8(data, filter->length, filter->start, true); break;
            case XX_RAR29_FILTER_ITANIUM: xx_rar29_filter_itanium(data, filter->length, filter->start); break;
            case XX_RAR29_FILTER_DELTA: {
                xx_rarx_status_t status = xx_rar29_filter_delta(data, filter->length, filter->registers[0]);
                if (status != XX_RARX_STATUS_OK) return status;
                break;
            }
            case XX_RAR29_FILTER_RGB: {
                xx_rarx_status_t status = xx_rar29_filter_rgb(data, filter->length, filter->registers[0], filter->registers[1]);
                if (status != XX_RARX_STATUS_OK) return status;
                break;
            }
            case XX_RAR29_FILTER_AUDIO: {
                xx_rarx_status_t status = xx_rar29_filter_audio(data, filter->length, filter->registers[0]);
                if (status != XX_RARX_STATUS_OK) return status;
                break;
            }
            default: return XX_RARX_STATUS_UNSUPPORTED_FILTER;
        }
    }
    return XX_RARX_STATUS_OK;
}

/*
 * Output model.  A member is always decoded into one contiguous destination
 * buffer, so the bytes already produced for this member are their own match
 * history.  The dictionary window of the state is therefore not written while
 * decoding: it keeps the history that existed when the member started (solid
 * continuation) and is only read for matches that reach back before the start
 * of this member.  xx_rar29_sync_window copies the tail of the produced data
 * into the window once, when decoding stops, so the state ends up exactly as
 * if every byte had also been stored in the window as it was produced.
 */
static inline xx_rarx_status_t xx_rar29_emit_byte(uint8_t *destination, size_t destination_size, size_t *produced, uint8_t byte)
{
    if (*produced >= destination_size) return XX_RARX_STATUS_CORRUPT;
    destination[(*produced)++] = byte;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rar29_emit_match(const xx_rarx29_state *state, uint8_t *destination, size_t destination_size, size_t *produced, uint32_t distance, uint32_t length)
{
    size_t position = *produced;
    size_t wsize = state->window_size;
    /* History available now: what the member started with plus what it has
     * produced, capped by the window size. */
    size_t history = position >= wsize - state->history_size ? wsize : state->history_size + position;
    size_t copied = 0;
    uint8_t *out;

    if (distance == 0 || distance > wsize || distance > history) return XX_RARX_STATUS_CORRUPT;
    if ((size_t)length > destination_size - position) return XX_RARX_STATUS_CORRUPT;
    out = destination + position;

    if ((size_t)distance > position) {
        /* The match starts in the history before this member. */
        size_t back = (size_t)distance - position; /* 1 .. history_size */
        size_t wpos = state->window_pos;
        size_t source = wpos >= back ? wpos - back : wsize - (back - wpos);
        size_t from_window = back < (size_t)length ? back : (size_t)length;
        const uint8_t *window = state->window;
        for (; copied < from_window; ++copied) {
            out[copied] = window[source];
            if (++source == wsize) source = 0;
        }
    }
    if (copied < (size_t)length) {
        size_t remaining = (size_t)length - copied;
        uint8_t *target = out + copied;
        const uint8_t *from = target - distance;
        if ((size_t)distance >= remaining) {
            xx_rt_memcpy(target, from, remaining);
        } else if (distance == 1) {
            xx_rt_memset(target, *from, remaining);
        } else {
            size_t j;
            for (j = 0; j < remaining; ++j) target[j] = from[j];
        }
    }
    *produced = position + (size_t)length;
    return XX_RARX_STATUS_OK;
}

/* Stores the bytes produced for this member in the dictionary window (see the
 * output model above).  Must run exactly once per decode call, before the
 * filters modify the destination: the window holds unfiltered data. */
static void xx_rar29_sync_window(xx_rarx29_state *state, const uint8_t *destination, size_t produced)
{
    size_t wsize = state->window_size;
    size_t wpos = state->window_pos;
    const uint8_t *source = destination;
    size_t count = produced;
    size_t first;

    if (produced == 0 || !state->window || wsize == 0) return;
    if (count > wsize) {
        /* Only the last window_size bytes survive. */
        size_t skipped = count - wsize;
        source += skipped;
        wpos = (wpos + skipped % wsize) % wsize;
        count = wsize;
    }
    first = wsize - wpos;
    if (first > count) first = count;
    xx_rt_memcpy(state->window + wpos, source, first);
    if (count > first) xx_rt_memcpy(state->window, source + first, count - first);
    wpos += count;
    if (wpos >= wsize) wpos -= wsize;
    state->window_pos = wpos;
    if (produced >= wsize - state->history_size) state->history_size = wsize;
    else state->history_size += produced;
}

static void xx_rar29_promote_distance(uint32_t distances[4], size_t index)
{
    uint32_t selected = distances[index];
    while (index > 0) {
        distances[index] = distances[index - 1u];
        --index;
    }
    distances[0] = selected;
}

static void xx_rar29_insert_distance(uint32_t distances[4], uint32_t value)
{
    distances[3] = distances[2];
    distances[2] = distances[1];
    distances[1] = distances[0];
    distances[0] = value;
}

static xx_rarx_status_t xx_rar29_decode_length(xx_rar29_bits *bits, const xx_rar29_huffman *code, uint32_t bias, uint32_t *length)
{
    uint32_t slot;
    uint32_t extra = 0;
    if (!xx_rar29_decode_symbol(bits, code, &slot)) return bits->error;
    if (slot >= XX_RAR29_LENGTH_SYMBOLS) return XX_RARX_STATUS_CORRUPT;
    if (xx_rar29_length_bits[slot] != 0 && !xx_rar29_read_bits(bits, xx_rar29_length_bits[slot], &extra)) return bits->error;
    *length = (uint32_t)xx_rar29_length_base[slot] + bias + extra;
    return XX_RARX_STATUS_OK;
}

static xx_rarx_status_t xx_rar29_decode_distance(xx_rarx29_state *state, xx_rar29_bits *bits, uint32_t *distance)
{
    uint32_t slot;
    uint32_t extra = 0;
    unsigned count;
    if (!xx_rar29_decode_symbol(bits, &state->codes[state->active_codes & 1u][XX_RAR29_CODE_DISTANCE], &slot)) return bits->error;
    if (slot >= XX_RAR29_DISTANCE_SYMBOLS) return XX_RARX_STATUS_CORRUPT;
    count = xx_rar29_distance_bits[slot];
    *distance = xx_rar29_distance_base[slot] + 1u;
    if (count == 0) return XX_RARX_STATUS_OK;
    if (slot > 9u) {
        if (count > 4u) {
            if (!xx_rar29_read_bits(bits, count - 4u, &extra)) return bits->error;
            if (extra > (UINT32_MAX >> 4)) return XX_RARX_STATUS_LIMIT;
            *distance += extra << 4;
        }
        if (state->low_distance_repeats != 0) {
            --state->low_distance_repeats;
            *distance += state->last_low_distance;
        } else {
            uint32_t low;
            if (!xx_rar29_decode_symbol(bits, &state->codes[state->active_codes & 1u][XX_RAR29_CODE_LOW], &low)) return bits->error;
            if (low == 16u) {
                state->low_distance_repeats = 15u;
                *distance += state->last_low_distance;
            } else if (low < 16u) {
                state->last_low_distance = low;
                *distance += low;
            } else {
                return XX_RARX_STATUS_CORRUPT;
            }
        }
    } else {
        if (!xx_rar29_read_bits(bits, count, &extra)) return bits->error;
        *distance += extra;
    }
    return XX_RARX_STATUS_OK;
}

/* Unaligned 16-byte copy; source and target may overlap when they are at
 * least 16 bytes apart (the whole source is loaded before anything is stored). */
static inline void xx_rar29_copy16(uint8_t *target, const uint8_t *source)
{
#if defined(XX_RAR29_USE_SSE2)
    _mm_storeu_si128((__m128i *)(void *)target, _mm_loadu_si128((const __m128i *)(const void *)source));
#else
    uint64_t low = xx_mem_load64(source);
    uint64_t high = xx_mem_load64(source + 8);
    xx_mem_store64(target, low);
    xx_mem_store64(target + 8, high);
#endif
}

/*
 * Fast LZ loop.  It decodes literals and matches with a 64-bit bit buffer and
 * no per-symbol bounds checks, and is only entered (and only keeps running)
 * while at least XX_RAR29_FAST_INPUT_MARGIN input bytes and
 * XX_RAR29_FAST_OUTPUT_MARGIN output bytes remain; `output_limit` is the
 * position at which it must stop.
 *
 * It never reports an error and never handles anything unusual itself.  When
 * it meets a symbol it does not handle (end of block / new tables, a filter
 * record, a code longer than the lookup table, or a match that fails
 * validation) it rewinds to the first bit of that symbol with the state
 * untouched by it and returns; the careful per-symbol code in
 * xx_rarx29_decode then decodes that symbol again and produces the result or
 * the error status.  Matches that reach into the history before this member
 * (solid continuation) are copied by the checked xx_rar29_emit_match.
 */
static void xx_rar29_lz_fast(xx_rarx29_state *state, xx_rar29_bits *bits, uint8_t *destination, size_t destination_size, size_t output_limit, size_t *produced)
{
    const unsigned set = state->active_codes & 1u;
    const uint32_t *main_table = state->main_packed[set];
    const uint32_t *distance_table = state->distance_packed[set];
    const uint16_t *low_table = state->codes[set][XX_RAR29_CODE_LOW].fast_table;
    const uint16_t *length_table = state->codes[set][XX_RAR29_CODE_LENGTH].fast_table;
    const uint8_t *data = bits->data;
    const uint8_t *in = data + (bits->bit >> 3);
    /* Highest position at which an iteration may start: its two refills read
     * at most 16 bytes from there. */
    const uint8_t *in_limit = data + (bits->size - XX_RAR29_FAST_INPUT_MARGIN);
    uint8_t *out = destination + *produced;
    uint8_t *const out_limit = destination + output_limit;
    const size_t wsize = state->window_size;
    uint64_t bitbuf = 0; /* next bits of the stream, first bit in bit 63 */
    unsigned bitcnt = 0; /* number of valid bits in bitbuf */
    uint32_t old0 = state->old_distance[0];
    uint32_t old1 = state->old_distance[1];
    uint32_t old2 = state->old_distance[2];
    uint32_t old3 = state->old_distance[3];
    uint32_t last_distance = state->last_distance;
    uint32_t last_length = state->last_length;
    uint32_t last_low = state->last_low_distance;
    uint32_t low_repeats = state->low_distance_repeats;

/* Tops the buffer up to 56..63 valid bits.  Bits below the valid count are
 * either zero or already the true following bits, so OR-ing is exact. */
#define XX_RAR29_REFILL()                                 \
    do {                                                  \
        bitbuf |= xx_data_get_be64(in) >> bitcnt;         \
        in += (63u - bitcnt) >> 3;                        \
        bitcnt |= 56u;                                    \
    } while (0)
/* The next n bits, 1 <= n <= 32. */
#define XX_RAR29_PEEK(n) ((uint32_t)(bitbuf >> (64u - (n))))
/* Like PEEK, but also valid for n == 0 (gives 0). */
#define XX_RAR29_PEEK0(n) ((uint32_t)((bitbuf >> 1) >> (63u - (n))))
#define XX_RAR29_DROP(n)   \
    do {                   \
        bitbuf <<= (n);    \
        bitcnt -= (n);     \
    } while (0)
/* Emits the literal of the main-table entry in `entry`. */
#define XX_RAR29_LITERAL()                                         \
    do {                                                           \
        *out++ = (uint8_t)(entry >> XX_RAR29_PACKED_SYMBOL_SHIFT); \
        XX_RAR29_DROP(entry & 63u);                                \
    } while (0)
#if XX_RAR29_MAIN_FAST_BITS > 11
#error "the literal run in xx_rar29_lz_fast assumes main-table codes of at most 11 bits"
#endif

    XX_RAR29_REFILL();
    {
        unsigned skip = (unsigned)(bits->bit & 7u);
        XX_RAR29_DROP(skip);
    }

    size_t idle_symbols = 0;
    while (in <= in_limit && out < out_limit) {
        uint32_t entry;
        uint32_t symbol;
        uint32_t value;
        uint32_t distance;
        uint32_t length;
        unsigned count;
        const uint8_t *symbol_in;
        unsigned symbol_bitcnt;
        uint32_t saved_last_low;
        uint32_t saved_low_repeats;

        XX_RAR29_REFILL();
        /* At least 56 bits.  A code found in the main table has at most
         * XX_RAR29_MAIN_FAST_BITS (11) bits, so up to five literals can be
         * decoded before the next refill: every lookup still sees at least 11
         * valid bits (56 - 4 * 11 = 12).  After one or two literals at least
         * 34 bits are left, enough for a non-literal main symbol with its
         * extra bits (11 + 6); after more literals a non-literal symbol is
         * simply looked up again at the top of the loop. */
        entry = main_table[XX_RAR29_PEEK(XX_RAR29_MAIN_FAST_BITS)];
        if (entry < XX_RAR29_PACKED_LITERAL_END) {
            XX_RAR29_LITERAL();
            entry = main_table[XX_RAR29_PEEK(XX_RAR29_MAIN_FAST_BITS)];
            if (entry < XX_RAR29_PACKED_LITERAL_END) {
                XX_RAR29_LITERAL();
                entry = main_table[XX_RAR29_PEEK(XX_RAR29_MAIN_FAST_BITS)];
                if (entry < XX_RAR29_PACKED_LITERAL_END) {
                    XX_RAR29_LITERAL();
                    entry = main_table[XX_RAR29_PEEK(XX_RAR29_MAIN_FAST_BITS)];
                    if (entry < XX_RAR29_PACKED_LITERAL_END) {
                        XX_RAR29_LITERAL();
                        entry = main_table[XX_RAR29_PEEK(XX_RAR29_MAIN_FAST_BITS)];
                        if (entry < XX_RAR29_PACKED_LITERAL_END) XX_RAR29_LITERAL();
                    }
                    continue;
                }
            }
        }

        /* Not a literal.  Remember where this symbol starts. */
        symbol_in = in;
        symbol_bitcnt = bitcnt;
        saved_last_low = last_low;
        saved_low_repeats = low_repeats;
        if (entry == XX_RAR29_PACKED_INVALID) goto bail;
        symbol = entry >> XX_RAR29_PACKED_SYMBOL_SHIFT;
        /* The entry carries the code length plus the number of extra bits that
         * follow the code, and the base those extra bits are added to: the
         * match length for 271.., the distance for 263..270, nothing else. */
        count = entry & 63u;
        value = ((entry >> 9) & 511u) + (XX_RAR29_PEEK(count) & ((1u << ((entry >> 6) & 7u)) - 1u));
        XX_RAR29_DROP(count);
        /* At least 56 bits again: enough for everything a match can still
         * read (a distance code with its high bits, 15 + 14, and a low
         * distance code, 15; or a length code with extra bits, 15 + 5). */
        XX_RAR29_REFILL();

        if (symbol >= 271u) {
            uint32_t extra;
            entry = distance_table[XX_RAR29_PEEK(XX_RAR29_DISTANCE_FAST_BITS)];
            if (entry == XX_RAR29_PACKED_INVALID) goto bail;
            count = entry & 63u;
            extra = XX_RAR29_PEEK(count) & ((1u << ((entry >> 6) & 15u)) - 1u);
            distance = entry >> 10; /* slot base + 1 */
            XX_RAR29_DROP(count);
            if (distance > 32u) {
                /* Slots above 9: the bits just read are the distance without
                 * its low four bits, which come from the low-distance code or
                 * repeat the previous ones. */
                distance += extra << 4;
                if (low_repeats != 0) {
                    --low_repeats;
                    distance += last_low;
                } else {
                    uint32_t low;
                    entry = low_table[XX_RAR29_PEEK(XX_RAR29_LOW_FAST_BITS)];
                    if (entry == XX_RAR29_FAST_INVALID) goto bail;
                    low = entry >> 4; /* at most 16 */
                    XX_RAR29_DROP(entry & 15u);
                    if (low == 16u) {
                        low_repeats = 15u;
                        distance += last_low;
                    } else {
                        last_low = low;
                        distance += low;
                    }
                }
            } else {
                distance += extra;
            }
            length = value + (distance >= 0x2000u ? 1u : 0u) + (distance >= 0x40000u ? 1u : 0u);
            if ((size_t)distance > (size_t)(out - destination) || (size_t)distance > wsize) goto history;
            old3 = old2;
            old2 = old1;
            old1 = old0;
            old0 = distance;
        } else if (symbol >= 263u) {
            distance = value;
            length = 2u;
            if ((size_t)distance > (size_t)(out - destination) || (size_t)distance > wsize) goto history;
            old3 = old2;
            old2 = old1;
            old1 = old0;
            old0 = distance;
        } else if (symbol >= 259u) {
            uint32_t slot;
            entry = length_table[XX_RAR29_PEEK(XX_RAR29_LENGTH_FAST_BITS)];
            if (entry == XX_RAR29_FAST_INVALID) goto bail;
            slot = entry >> 4; /* below 28 */
            XX_RAR29_DROP(entry & 15u);
            count = xx_rar29_length_bits[slot];
            length = (uint32_t)xx_rar29_length_base[slot] + 2u + XX_RAR29_PEEK0(count);
            XX_RAR29_DROP(count);
            if (symbol == 259u) {
                distance = old0;
            } else if (symbol == 260u) {
                distance = old1;
            } else if (symbol == 261u) {
                distance = old2;
            } else {
                distance = old3;
            }
            if (distance == 0 || (size_t)distance > (size_t)(out - destination) || (size_t)distance > wsize) goto history;
            /* Move the selected distance to the front. */
            if (symbol >= 262u) old3 = old2;
            if (symbol >= 261u) old2 = old1;
            if (symbol >= 260u) old1 = old0;
            old0 = distance;
        } else if (symbol == 258u) {
            if (last_length == 0) {
                /* A repeat with no previous match produces nothing, so the
                 * output-based burst bound does not limit a run of them.
                 * Leave long runs to the careful code, which polls for
                 * cancellation. */
                if (++idle_symbols > XX_RAR29_FAST_IDLE_LIMIT) goto bail;
                continue;
            }
            distance = last_distance;
            length = last_length;
            if (length > XX_RAR29_FAST_MAX_MATCH) goto bail;
            if (distance == 0 || (size_t)distance > (size_t)(out - destination) || (size_t)distance > wsize) goto history;
        } else {
            /* 256 (end of block / new tables) or 257 (filter record). */
            goto bail;
        }

        /* Copy the match.  out + length + 32 stays inside the destination
         * (output margin), so the copies may overshoot the match end; the
         * overshoot is overwritten by the following output. */
        {
            const uint8_t *from = out - distance;
            uint8_t *const end = out + length;
            if (distance >= 16u) {
                /* Most matches are short: copy 32 bytes without looking at
                 * the length. */
                xx_rar29_copy16(out, from);
                xx_rar29_copy16(out + 16, from + 16);
                if (length > 32u) {
                    out += 32;
                    from += 32;
                    do {
                        xx_rar29_copy16(out, from);
                        out += 16;
                        from += 16;
                    } while (out < end);
                }
            } else if (distance >= 8u) {
                do {
                    xx_mem_store64(out, xx_mem_load64(from));
                    out += 8;
                    from += 8;
                } while (out < end);
            } else if (distance == 1u) {
                uint64_t pattern = (uint64_t)from[0] * 0x0101010101010101ull;
                do {
                    xx_mem_store64(out, pattern);
                    out += 8;
                } while (out < end);
            } else {
                do {
                    *out++ = *from++;
                } while (out < end);
            }
            out = end;
        }
        last_distance = distance;
        last_length = length;
        continue;

    history:
        /* The distance is zero, larger than the window, or reaches before this
         * member (solid continuation).  The checked copy either takes the
         * bytes from the dictionary window or rejects the match; a rejected
         * match is left to the careful code, which reports the error.  The
         * distance history has not been updated yet for this symbol. */
        {
            size_t position = (size_t)(out - destination);
            if (xx_rar29_emit_match(state, destination, destination_size, &position, distance, length) != XX_RARX_STATUS_OK) goto bail;
            out = destination + position;
            if (symbol >= 263u) {
                old3 = old2;
                old2 = old1;
                old1 = old0;
                old0 = distance;
            } else if (symbol >= 259u) {
                if (symbol >= 262u) old3 = old2;
                if (symbol >= 261u) old2 = old1;
                if (symbol >= 260u) old1 = old0;
                old0 = distance;
            }
            last_distance = distance;
            last_length = length;
            continue;
        }

    bail:
        in = symbol_in;
        bitcnt = symbol_bitcnt;
        last_low = saved_last_low;
        low_repeats = saved_low_repeats;
        break;
    }

#undef XX_RAR29_REFILL
#undef XX_RAR29_PEEK
#undef XX_RAR29_PEEK0
#undef XX_RAR29_DROP
#undef XX_RAR29_LITERAL

    /* `in` is the first byte not loaded into the bit buffer. */
    bits->bit = (size_t)(in - data) * 8u - bitcnt;
    *produced = (size_t)(out - destination);
    state->old_distance[0] = old0;
    state->old_distance[1] = old1;
    state->old_distance[2] = old2;
    state->old_distance[3] = old3;
    state->last_distance = last_distance;
    state->last_length = last_length;
    state->last_low_distance = last_low;
    state->low_distance_repeats = low_repeats;
}

static xx_rarx_status_t xx_rar29_prepare_window(xx_rarx29_state *state, size_t window_size, bool solid)
{
    uint8_t *window;
    if (window_size == 0) return XX_RARX_STATUS_INVALID_ARGUMENT;
    if (solid) {
        if (!state->window || state->window_size != window_size || (!state->tables_valid && !state->ppm_model_valid)) return XX_RARX_STATUS_CORRUPT;
        return XX_RARX_STATUS_OK;
    }
    xx_rarx29_reset(state);
    window = (uint8_t *)xx_mem_calloc(window_size, 1u);
    if (!window) return XX_RARX_STATUS_NO_MEMORY;
    state->window = window;
    state->window_size = window_size;
    state->table_required = true;
    return XX_RARX_STATUS_OK;
}

xx_rarx29_state *xx_rarx29_create(void)
{
    xx_rarx29_state *state = (xx_rarx29_state *)xx_mem_calloc(1, sizeof(xx_rarx29_state));
    if (state) {
        Ppmd7_Construct(&state->ppm_model);
        state->ppm_escape = 2u;
    }
    return state;
}

void xx_rarx29_reset(xx_rarx29_state *state)
{
    if (!state) return;
    xx_rar29_clear_ppm_storage(&state->ppm_model);
    Ppmd7_Free(&state->ppm_model);
    xx_rarx_clear_free(state->window, state->window_size);
    xx_rarx_clear_free(state->programs, state->program_capacity * sizeof(*state->programs));
    xx_mem_zero(state, sizeof(*state));
    Ppmd7_Construct(&state->ppm_model);
    state->ppm_escape = 2u;
}

void xx_rarx29_destroy(xx_rarx29_state *state)
{
    if (!state) return;
    xx_rarx29_reset(state);
    xx_rarx_clear_free(state, sizeof(*state));
}

xx_rarx_status_t xx_rarx29_decode(xx_rarx29_state *state, const uint8_t *source, size_t source_size, uint8_t *destination, size_t destination_size, size_t window_size,
                                  size_t allocation_limit, bool solid, size_t *source_used, xx_pd_struct *progress)
{
    xx_rar29_bits bits;
    xx_rar29_ppm_range ppm_coder;
    xx_rar29_filter_list filters;
    xx_rarx_status_t status;
    size_t produced = 0;
    size_t cancel_mark = 0;
    uint64_t operations = 0;
    bool finished = false;
    bool ppm_mode = false;
    bool window_prepared = false;

    if (source_used) *source_used = 0;
    if (!state || (!source && source_size != 0) || (!destination && destination_size != 0) || window_size == 0 || allocation_limit == 0)
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    if (window_size > allocation_limit) return XX_RARX_STATUS_LIMIT;
    /* The bit reader counts bits in a size_t and rounds bit counts up to
     * bytes: refuse inputs whose bit count, plus that rounding and a 64-bit
     * read, would not fit (packed members near 512 MiB on 32-bit builds). */
    if (source_size > (SIZE_MAX - 128u) / 8u) return XX_RARX_STATUS_LIMIT;
    if (progress && xx_pd_is_stopped(progress)) return XX_RARX_STATUS_CANCELLED;

    status = xx_rar29_prepare_window(state, window_size, solid);
    if (status != XX_RARX_STATUS_OK) return status;
    window_prepared = true;
    xx_rt_memset(&bits, 0, sizeof(bits));
    bits.data = source;
    bits.size = source_size;
    bits.error = XX_RARX_STATUS_OK;
    xx_rt_memset(&ppm_coder, 0, sizeof(ppm_coder));
    xx_rt_memset(&filters, 0, sizeof(filters));

    if (!state->tables_valid || state->table_required) {
        status = xx_rar29_read_tables(state, &bits, &ppm_mode);
        if (status != XX_RARX_STATUS_OK) goto done;
        if (ppm_mode) {
            status = xx_rar29_ppm_begin(state, &bits, &ppm_coder, allocation_limit);
            if (status != XX_RARX_STATUS_OK) goto done;
        }
    }

    while (!finished) {
        uint32_t symbol;
        uint32_t distance = 0;
        uint32_t length = 0;

        if ((operations++ & 0x3fffu) == 0 && progress && xx_pd_is_stopped(progress)) {
            status = XX_RARX_STATUS_CANCELLED;
            goto done;
        }
        if (!ppm_mode && destination_size - produced > XX_RAR29_FAST_OUTPUT_MARGIN && bits.size >= XX_RAR29_FAST_INPUT_MARGIN && bits.size <= SIZE_MAX / 8u &&
            (bits.bit >> 3) <= bits.size - XX_RAR29_FAST_INPUT_MARGIN) {
            /* Ample input and output left: decode literals and matches in the
             * fast loop until it meets something it leaves to the code below.
             * Bursts are bounded so that cancellation stays responsive. */
            size_t output_limit = destination_size - XX_RAR29_FAST_OUTPUT_MARGIN;
            if (output_limit - produced > XX_RAR29_FAST_BURST) output_limit = produced + XX_RAR29_FAST_BURST;
            size_t produced_before = produced;
            xx_rar29_lz_fast(state, &bits, destination, destination_size, output_limit, &produced);
            /* A burst without output (a long run of empty repeats) is polled
             * too: the output-based mark alone would not advance. */
            if (progress && (produced >= cancel_mark || produced == produced_before)) {
                if (xx_pd_is_stopped(progress)) {
                    status = XX_RARX_STATUS_CANCELLED;
                    goto done;
                }
                cancel_mark = produced + 0x10000u;
            }
        }
        if (ppm_mode) {
            uint8_t byte;
            status = xx_rar29_ppm_decode_symbol(state, &ppm_coder, &byte);
            if (status != XX_RARX_STATUS_OK) goto done;
            if (byte != state->ppm_escape) {
                status = xx_rar29_emit_byte(destination, destination_size, &produced, byte);
                if (status != XX_RARX_STATUS_OK) goto done;
                continue;
            }

            status = xx_rar29_ppm_decode_symbol(state, &ppm_coder, &byte);
            if (status != XX_RARX_STATUS_OK) goto done;
            if (byte == 0u) {
                status = xx_rar29_read_tables(state, &bits, &ppm_mode);
                if (status != XX_RARX_STATUS_OK) goto done;
                if (ppm_mode) {
                    status = xx_rar29_ppm_begin(state, &bits, &ppm_coder, allocation_limit);
                    if (status != XX_RARX_STATUS_OK) goto done;
                }
                continue;
            }
            if (byte == 2u) {
                state->table_required = true;
                finished = true;
                continue;
            }
            if (byte == 3u) {
                /* Standard filters are supported in LZ blocks.  PPMd embeds
                 * the entire filter record in the model-coded byte stream. */
                status = XX_RARX_STATUS_UNSUPPORTED_FILTER;
                goto done;
            }
            if (byte == 4u) {
                uint8_t fields[4];
                unsigned field;
                for (field = 0; field < 4u; ++field) {
                    status = xx_rar29_ppm_decode_symbol(state, &ppm_coder, fields + field);
                    if (status != XX_RARX_STATUS_OK) goto done;
                }
                distance = ((uint32_t)fields[0] << 16) | ((uint32_t)fields[1] << 8) | fields[2];
                distance += 2u;
                length = (uint32_t)fields[3] + 32u;
                status = xx_rar29_emit_match(state, destination, destination_size, &produced, distance, length);
                if (status != XX_RARX_STATUS_OK) goto done;
                continue;
            }
            if (byte == 5u) {
                uint8_t encoded_length;
                status = xx_rar29_ppm_decode_symbol(state, &ppm_coder, &encoded_length);
                if (status != XX_RARX_STATUS_OK) goto done;
                status = xx_rar29_emit_match(state, destination, destination_size, &produced, 1u, (uint32_t)encoded_length + 4u);
                if (status != XX_RARX_STATUS_OK) goto done;
                continue;
            }

            /* Escape action 1 and all reserved values >= 6 encode the escape
             * byte itself as a literal. */
            status = xx_rar29_emit_byte(destination, destination_size, &produced, state->ppm_escape);
            if (status != XX_RARX_STATUS_OK) goto done;
            continue;
        }

        if (!xx_rar29_decode_symbol(&bits, &state->codes[state->active_codes & 1u][XX_RAR29_CODE_MAIN], &symbol)) {
            status = bits.error;
            goto done;
        }
        if (symbol < 256u) {
            status = xx_rar29_emit_byte(destination, destination_size, &produced, (uint8_t)symbol);
            if (status != XX_RARX_STATUS_OK) goto done;
            continue;
        }
        if (symbol == 256u) {
            uint32_t new_block;
            if (!xx_rar29_read_bits(&bits, 1, &new_block)) {
                status = bits.error;
                goto done;
            }
            if (new_block != 0) {
                status = xx_rar29_read_tables(state, &bits, &ppm_mode);
                if (status != XX_RARX_STATUS_OK) goto done;
                if (ppm_mode) {
                    status = xx_rar29_ppm_begin(state, &bits, &ppm_coder, allocation_limit);
                    if (status != XX_RARX_STATUS_OK) goto done;
                }
            } else {
                uint32_t new_table;
                if (!xx_rar29_read_bits(&bits, 1, &new_table)) {
                    status = bits.error;
                    goto done;
                }
                state->table_required = new_table != 0;
                finished = true;
            }
            continue;
        }
        if (symbol == 257u) {
            status = xx_rar29_read_filter(state, &bits, produced, destination_size, &filters);
            if (status != XX_RARX_STATUS_OK) goto done;
            continue;
        }
        if (symbol == 258u) {
            if (state->last_length == 0) continue;
            distance = state->last_distance;
            length = state->last_length;
        } else if (symbol <= 262u) {
            size_t index = (size_t)(symbol - 259u);
            distance = state->old_distance[index];
            status = xx_rar29_decode_length(&bits, &state->codes[state->active_codes & 1u][XX_RAR29_CODE_LENGTH], 2u, &length);
            if (status != XX_RARX_STATUS_OK) goto done;
            xx_rar29_promote_distance(state->old_distance, index);
        } else if (symbol <= 270u) {
            size_t index = (size_t)(symbol - 263u);
            uint32_t extra = 0;
            if (!xx_rar29_read_bits(&bits, xx_rar29_short_bits[index], &extra)) {
                status = bits.error;
                goto done;
            }
            distance = (uint32_t)xx_rar29_short_base[index] + 1u + extra;
            length = 2u;
            xx_rar29_insert_distance(state->old_distance, distance);
        } else {
            uint32_t slot = symbol - 271u;
            uint32_t extra = 0;
            if (slot >= XX_RAR29_LENGTH_SYMBOLS) {
                status = XX_RARX_STATUS_CORRUPT;
                goto done;
            }
            if (xx_rar29_length_bits[slot] != 0 && !xx_rar29_read_bits(&bits, xx_rar29_length_bits[slot], &extra)) {
                status = bits.error;
                goto done;
            }
            length = (uint32_t)xx_rar29_length_base[slot] + 3u + extra;
            status = xx_rar29_decode_distance(state, &bits, &distance);
            if (status != XX_RARX_STATUS_OK) goto done;
            if (distance >= 0x2000u) ++length;
            if (distance >= 0x40000u) ++length;
            xx_rar29_insert_distance(state->old_distance, distance);
        }

        status = xx_rar29_emit_match(state, destination, destination_size, &produced, distance, length);
        if (status != XX_RARX_STATUS_OK) goto done;
        state->last_distance = distance;
        state->last_length = length;
    }

    if (produced != destination_size) {
        status = XX_RARX_STATUS_CORRUPT;
        goto done;
    }
    /* The window receives the unfiltered data, so store it before the filters
     * run. */
    xx_rar29_sync_window(state, destination, produced);
    window_prepared = false;
    status = xx_rar29_apply_filters(destination, destination_size, &filters);

done:
    /* Every other way out (errors, cancellation): keep the dictionary state
     * consistent with the bytes that were produced. */
    if (window_prepared) xx_rar29_sync_window(state, destination, produced);
    if (source_used) {
        size_t bytes = bits.bit / 8u + ((bits.bit & 7u) != 0 ? 1u : 0u);
        if (bytes > source_size) bytes = source_size;
        *source_used = bytes;
    }
    xx_rarx_clear_free(filters.items, filters.capacity * sizeof(*filters.items));
    xx_mem_zero(&ppm_coder, sizeof(ppm_coder));
    xx_mem_zero(&bits, sizeof(bits));
    return status;
}
