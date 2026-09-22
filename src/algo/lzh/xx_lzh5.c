/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LHA -lh4-/-lh5-/-lh6-/-lh7-.
 *
 * One algorithm, four window sizes. A stream is a sequence of blocks; each
 * block declares its own Huffman tables and then the number of symbols coded
 * with them:
 *
 *   u16   symbol count for this block
 *   pre-table   19 code lengths, themselves run-length coded
 *   literal table   up to 510 code lengths, coded with the pre-table
 *   position table  up to w_bits+1 code lengths, coded with its own pre-table
 *   symbols     count of them
 *
 * A symbol below 256 is a literal. At or above it, the symbol encodes a match
 * length (symbol - 256 + 3) and is followed by a position code: code 0 means
 * distance 1, and code p > 1 means distance (1 << (p-1)) + (p-1 raw bits) + 1.
 *
 * Two deliberate departures from the reference, neither of which changes the
 * bytes produced:
 *
 * Canonical Huffman rather than a direct-index table with a binary tree for
 * overlong codes. The reference's tables are built by walking symbols in order
 * and handing out consecutive bit patterns per length, which is the definition
 * of a canonical code -- so decoding it canonically gives the same symbol for
 * the same bits, in a third of the lines. The reference's table/tree split is
 * a speed optimisation, and this decoder is not on a hot path.
 *
 * The output buffer is its own window. The reference keeps a separate 128 KiB
 * ring and copies out of it; here a back-reference reads the bytes already
 * decoded, which is the same history. The one case that needs care is a
 * reference pointing before the start of output -- legal in LHA, where the
 * window begins pre-filled with spaces -- so that yields 0x20, as it does
 * there.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzh/xx_lzh.h"

#include "xxfclib/memory/xx_memory.h"

#define XX_LZH5_MIN_MATCH 3
#define XX_LZH5_MAX_MATCH 256
/* 256 literals + (256 - 3 + 1) lengths */
#define XX_LZH5_LT_SIZE (256 + XX_LZH5_MAX_MATCH - XX_LZH5_MIN_MATCH + 1)
#define XX_LZH5_PT_SIZE 19
#define XX_LZH5_MAX_BITS 16
/* Window pre-fill, which is what a reference before the start of output
 * resolves to. */
#define XX_LZH5_WINDOW_FILL 0x20U

typedef struct xx_lzh5_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position; /* next byte to load */
    uint32_t cache;
    int available; /* bits currently in cache */
    bool overrun;  /* a read went past the end of input */
} xx_lzh5_bits;

/*
 * A canonical Huffman table: symbols sorted by (length, symbol), with the
 * first code and first symbol index recorded per length.
 */
typedef struct xx_lzh5_huff_s {
    int count[XX_LZH5_MAX_BITS + 1];
    int first_code[XX_LZH5_MAX_BITS + 1];
    int first_index[XX_LZH5_MAX_BITS + 1];
    uint16_t symbols[XX_LZH5_LT_SIZE];
    int max_bits;
    /* A table with exactly one symbol carries no bits at all: every lookup
     * returns that symbol without consuming input. */
    bool single;
    uint16_t single_symbol;
} xx_lzh5_huff;

static void xx_lzh5_bits_init(xx_lzh5_bits *bits, const uint8_t *data,
                              size_t size) {
    bits->data = data;
    bits->size = size;
    bits->position = 0U;
    bits->cache = 0U;
    bits->available = 0;
    bits->overrun = false;
}

/*
 * Past the end of input the reader yields zero bits and raises the overrun
 * flag. LHA encoders pad the final byte, so a few zero bits at the end are
 * normal; the flag only matters when the caller has not yet produced the
 * expected output, which is checked at the end.
 */
static uint32_t xx_lzh5_read_bits(xx_lzh5_bits *bits, int count) {
    uint32_t result;

    if (count <= 0) return 0U;
    while (bits->available < count) {
        uint8_t next = 0U;
        if (bits->position < bits->size) {
            next = bits->data[bits->position++];
        } else {
            bits->overrun = true;
        }
        bits->cache = (bits->cache << 8) | next;
        bits->available += 8;
    }
    result = (bits->cache >> (bits->available - count)) &
             ((count >= 32) ? 0xFFFFFFFFU : ((1U << count) - 1U));
    bits->available -= count;
    return result;
}

static uint32_t xx_lzh5_peek_bits(xx_lzh5_bits *bits, int count) {
    uint32_t result = xx_lzh5_read_bits(bits, count);

    bits->available += count;
    return result;
}

/*
 * Build a canonical table from code lengths. Rejects anything that is not a
 * complete code, which is the same Kraft-equality test the reference spells
 * as "the accumulated bit patterns must total 0x10000".
 */
static bool xx_lzh5_build(xx_lzh5_huff *table, const uint8_t *lengths,
                          int count) {
    int length;
    int index;
    int code = 0;
    int total = 0;
    int next_index[XX_LZH5_MAX_BITS + 1];

    xx_mem_zero(table, sizeof(*table));
    for (index = 0; index < count; ++index) {
        if (lengths[index] > XX_LZH5_MAX_BITS) return false;
        if (lengths[index] != 0U) {
            ++table->count[lengths[index]];
            ++total;
        }
    }
    if (total == 0) return false;
    if (total == 1) {
        for (index = 0; index < count; ++index) {
            if (lengths[index] != 0U) {
                table->single = true;
                table->single_symbol = (uint16_t)index;
                return true;
            }
        }
        return false;
    }

    for (length = 1; length <= XX_LZH5_MAX_BITS; ++length) {
        table->first_code[length] = code;
        table->first_index[length] = (length == 1)
                                         ? 0
                                         : table->first_index[length - 1] +
                                               table->count[length - 1];
        next_index[length] = table->first_index[length];
        code += table->count[length];
        /* An over-subscribed code would make this exceed the space available
         * at the next length. */
        if (code > (1 << length)) return false;
        code <<= 1;
        if (table->count[length] != 0) table->max_bits = length;
    }
    /* code has been shifted once past max_bits; a complete code fills it. */
    if (code != (1 << (XX_LZH5_MAX_BITS + 1))) {
        /* Incomplete codes are rejected, matching the reference. */
        return false;
    }

    for (index = 0; index < count; ++index) {
        uint8_t length_value = lengths[index];
        if (length_value != 0U) {
            table->symbols[next_index[length_value]++] = (uint16_t)index;
        }
    }
    return true;
}

static int xx_lzh5_decode_symbol(xx_lzh5_bits *bits,
                                 const xx_lzh5_huff *table) {
    int length;
    int code = 0;

    if (table->single) return (int)table->single_symbol;
    for (length = 1; length <= table->max_bits; ++length) {
        code = (code << 1) | (int)xx_lzh5_read_bits(bits, 1);
        if (table->count[length] != 0 &&
            code - table->first_code[length] < table->count[length]) {
            return (int)table->symbols[table->first_index[length] +
                                       (code - table->first_code[length])];
        }
    }
    return -1;
}

/*
 * The pre-table's own lengths use a prefix code: 0..6 are three bits, and
 * seven onwards are "111" followed by that many extra 1 bits and a 0, up to
 * 16. The reference resolves this through a 1024-entry lookup; counting the
 * leading ones is the same function.
 */
static int xx_lzh5_read_pt_length(xx_lzh5_bits *bits) {
    int value = (int)xx_lzh5_read_bits(bits, 3);
    int extra = 0;

    if (value != 7) return value;
    while (extra < 10 && xx_lzh5_read_bits(bits, 1) != 0U) ++extra;
    if (extra >= 10) return -1; /* would exceed 16 */
    return 7 + extra;
}

/* Read a table of code lengths that is itself coded with the prefix code. */
static bool xx_lzh5_read_pt(xx_lzh5_bits *bits, xx_lzh5_huff *table,
                            int size, int count_bits, bool is_position) {
    uint8_t lengths[XX_LZH5_PT_SIZE > 32 ? XX_LZH5_PT_SIZE : 32];
    int available = (int)xx_lzh5_read_bits(bits, count_bits);
    int index = 0;

    if (available == 0) {
        /* No lengths: the whole table is one symbol, named outright. */
        int symbol = (int)xx_lzh5_read_bits(bits, count_bits);
        if (symbol >= size) return false;
        xx_mem_zero(table, sizeof(*table));
        table->single = true;
        table->single_symbol = (uint16_t)symbol;
        return true;
    }
    if (available > size) return false;
    xx_mem_zero(lengths, sizeof(lengths));

    while (index < available) {
        int length = xx_lzh5_read_pt_length(bits);
        if (length < 0) return false;
        lengths[index++] = (uint8_t)length;
        /* Only the literal pre-table carries the three-entry escape: after
         * the first three lengths a two-bit count says how many entries are
         * zero. The position table has no such rule. */
        if (!is_position && index == 3) {
            int skip = (int)xx_lzh5_read_bits(bits, 2);
            if (skip > available - 3) return false;
            while (skip-- > 0) lengths[index++] = 0U;
        }
        if (bits->overrun) return false;
    }
    return xx_lzh5_build(table, lengths, available);
}

/* Read the literal/length table, which is coded with the pre-table. */
static bool xx_lzh5_read_literal(xx_lzh5_bits *bits,
                                 const xx_lzh5_huff *pre,
                                 xx_lzh5_huff *table) {
    uint8_t lengths[XX_LZH5_LT_SIZE];
    int available = (int)xx_lzh5_read_bits(bits, 9);
    int index = 0;

    if (available == 0) {
        int symbol = (int)xx_lzh5_read_bits(bits, 9);
        if (symbol >= XX_LZH5_LT_SIZE) return false;
        xx_mem_zero(table, sizeof(*table));
        table->single = true;
        table->single_symbol = (uint16_t)symbol;
        return true;
    }
    if (available > XX_LZH5_LT_SIZE) return false;
    xx_mem_zero(lengths, sizeof(lengths));

    while (index < available) {
        int symbol = xx_lzh5_decode_symbol(bits, pre);
        if (symbol < 0 || bits->overrun) return false;
        if (symbol > 2) {
            /* Codes three and up are a length, biased by two. */
            lengths[index++] = (uint8_t)(symbol - 2);
        } else if (symbol == 0) {
            lengths[index++] = 0U;
        } else {
            /* One and two introduce a run of zero lengths: a four-bit count
             * biased by three, or a nine-bit count biased by twenty. */
            int width = (symbol == 1) ? 4 : 9;
            int run = (int)xx_lzh5_read_bits(bits, width) +
                      ((width == 4) ? 3 : 20);
            if (index + run > available) return false;
            while (run-- > 0) lengths[index++] = 0U;
        }
    }
    return xx_lzh5_build(table, lengths, available);
}

bool xx_lzh5_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size, int method,
                           size_t *written) {
    xx_lzh5_bits bits;
    xx_lzh5_huff pre;
    xx_lzh5_huff literal;
    xx_lzh5_huff position;
    size_t produced = 0U;
    int window_bits;
    int position_size;
    int position_count_bits;

    if (written) *written = 0U;
    if (!input || !output) return false;

    switch (method) {
        case 4: window_bits = 12; break;
        case 5: window_bits = 13; break;
        case 6: window_bits = 15; break;
        case 7: window_bits = 16; break;
        default: return false;
    }
    position_size = window_bits + 1;
    /* The wider windows need a five-bit count for the position table. */
    position_count_bits = (window_bits >= 15) ? 5 : 4;

    if (output_size == 0U) return true;
    xx_lzh5_bits_init(&bits, input, input_size);

    while (produced < output_size) {
        int block_symbols = (int)xx_lzh5_read_bits(&bits, 16);

        if (bits.overrun) return false;
        if (block_symbols == 0) return false;

        if (!xx_lzh5_read_pt(&bits, &pre, XX_LZH5_PT_SIZE, 5, false) ||
            !xx_lzh5_read_literal(&bits, &pre, &literal) ||
            !xx_lzh5_read_pt(&bits, &position, position_size,
                             position_count_bits, true)) {
            return false;
        }

        while (block_symbols-- > 0 && produced < output_size) {
            int symbol = xx_lzh5_decode_symbol(&bits, &literal);

            if (symbol < 0 || bits.overrun) return false;
            if (symbol < 256) {
                output[produced++] = (uint8_t)symbol;
                continue;
            }
            {
                int length = symbol - 256 + XX_LZH5_MIN_MATCH;
                int code = xx_lzh5_decode_symbol(&bits, &position);
                size_t distance;
                size_t index;

                if (code < 0 || code >= position_size || bits.overrun) {
                    return false;
                }
                if (code <= 1) {
                    distance = (size_t)code + 1U;
                } else {
                    distance = ((size_t)1U << (code - 1)) +
                               (size_t)xx_lzh5_read_bits(&bits, code - 1) + 1U;
                }
                if (bits.overrun) return false;
                if (distance > ((size_t)1U << window_bits)) return false;

                for (index = 0; index < (size_t)length && produced < output_size;
                     ++index) {
                    /* Before the start of output the window was pre-filled
                     * with spaces; reproduce that rather than reading out of
                     * bounds. */
                    output[produced] = (distance > produced)
                                           ? (uint8_t)XX_LZH5_WINDOW_FILL
                                           : output[produced - distance];
                    ++produced;
                }
            }
        }
    }

    if (written) *written = produced;
    return produced == output_size;
}
