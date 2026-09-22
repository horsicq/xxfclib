/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Squeeze It (HLSQZ) methods 1..4.
 *
 * Ported from XArchive/Algos/xsqzdecoder.cpp (XSQZDecoder::decompress), which
 * is the reference for this codec.  Structure, table contents and every
 * validation are kept one-for-one; the only changes are mechanical: Qt device
 * I/O becomes flat buffers, C++ containers become plain arrays, and the
 * decoder state lives in one heap struct the caller never sees (no static
 * mutable state, so this is safe to call from several threads at once).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/sqz/xx_sqz.h"

#define SQZ_WINDOW_SIZE 0x8000
#define SQZ_WINDOW_MASK (SQZ_WINDOW_SIZE - 1)
#define SQZ_NT 19
#define SQZ_NC 0x1ff
#define SQZ_NP 0x1f
#define SQZ_MAX_BITS 16
#define SQZ_MAX_PADDING_BYTES 2

/* Methods 3/4 map C symbols 0x100..0x11f through these tables. */
static const uint8_t sqz_length_extra[32] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6};
static const uint16_t sqz_length_base[32] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   10,  12,  14,  16,  20,  24,  28,
    32,  40,  48,  56,  64,  80,  96,  112, 128, 160, 192, 224, 256, 320, 384, 448};

/* Methods 2/4 use the distance tables embedded in SQZ.EXE.  Index 31 is not
 * part of the 31-symbol P alphabet and is intentionally omitted. */
static const uint8_t sqz_distance_extra[31] = {
    0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const uint16_t sqz_distance_base[31] = {
    0,   1,   2,   3,   4,    5,    7,    9,    13,   17,   25,   33,   49,   65,   97,   129,
    193, 257, 385, 513, 769,  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

typedef struct sqz_huffman {
    bool constant;
    uint16_t constant_symbol;
    int32_t symbol_count;
    uint16_t counts[SQZ_MAX_BITS + 1];
    uint32_t first_code[SQZ_MAX_BITS + 1];
    uint16_t first_symbol[SQZ_MAX_BITS + 1];
    uint16_t symbols[SQZ_NC];
} sqz_huffman;

typedef struct sqz_state {
    const uint8_t *input;
    size_t input_size;
    size_t input_pos;
    uint8_t *output;
    size_t raw_size;   /* the member's declared uncompressed size */
    size_t produced;

    uint32_t method;
    uint32_t block_remaining;
    uint32_t bit_buffer;
    int32_t bit_count;
    int32_t padding_bytes;
    int32_t window_position;
    bool error;

    uint8_t window[SQZ_WINDOW_SIZE];
    uint8_t code_lengths[SQZ_NC];
    sqz_huffman pt_tree;
    sqz_huffman c_tree;
    sqz_huffman p_tree;
} sqz_state;

/* ----------------------------------------------------------- huffman --- */

static bool sqz_read_bits(sqz_state *s, int32_t bits, uint32_t *value);

static bool sqz_huff_set_constant(sqz_huffman *t, uint32_t symbol,
                                  int32_t count)
{
    if (!t || (count <= 0) || (count > SQZ_NC) ||
        (symbol >= (uint32_t)count)) {
        return false;
    }
    t->constant = true;
    t->constant_symbol = (uint16_t)symbol;
    t->symbol_count = count;
    return true;
}

static bool sqz_huff_build(sqz_huffman *t, const uint8_t *lengths,
                           int32_t count)
{
    int32_t i;
    int32_t length;
    int32_t symbol;
    int32_t codes_left;
    uint32_t code;
    uint32_t symbol_index;

    if (!t || !lengths || (count <= 0) || (count > SQZ_NC)) return false;

    t->constant = false;
    t->constant_symbol = 0;
    t->symbol_count = 0;
    xx_rt_memset(t->counts, 0, sizeof(t->counts));
    xx_rt_memset(t->first_code, 0, sizeof(t->first_code));
    xx_rt_memset(t->first_symbol, 0, sizeof(t->first_symbol));
    xx_rt_memset(t->symbols, 0, sizeof(t->symbols));

    for (i = 0; i < count; ++i) {
        length = (int32_t)lengths[i];
        if (length > SQZ_MAX_BITS) return false;
        if (length != 0) ++t->counts[length];
    }

    /* SQZ.EXE's table builder requires the 16-bit canonical code space to be
     * exactly filled.  Constant alphabets use the separate n==0 form. */
    codes_left = 1;
    for (length = 1; length <= SQZ_MAX_BITS; ++length) {
        codes_left = (codes_left << 1) - (int32_t)t->counts[length];
        if (codes_left < 0) return false;
    }
    if (codes_left != 0) return false;

    code = 0;
    symbol_index = 0;
    for (length = 1; length <= SQZ_MAX_BITS; ++length) {
        code = (code + t->counts[length - 1]) << 1;
        t->first_code[length] = code;
        t->first_symbol[length] = (uint16_t)symbol_index;

        for (symbol = 0; symbol < count; ++symbol) {
            if ((int32_t)lengths[symbol] == length) {
                if (symbol_index >= (uint32_t)count) return false;
                t->symbols[symbol_index++] = (uint16_t)symbol;
            }
        }
    }

    t->symbol_count = count;
    return (symbol_index > 0) && (symbol_index <= (uint32_t)count);
}

static bool sqz_huff_decode(const sqz_huffman *t, sqz_state *s,
                            uint32_t *symbol)
{
    int32_t length;
    uint32_t code;
    uint32_t bit;
    uint32_t first;
    uint32_t count;
    uint32_t index;

    if (!t || !s || !symbol || (t->symbol_count <= 0)) return false;
    if (t->constant) {
        *symbol = t->constant_symbol;
        return (int32_t)t->constant_symbol < t->symbol_count;
    }

    code = 0;
    for (length = 1; length <= SQZ_MAX_BITS; ++length) {
        bit = 0;
        if (!sqz_read_bits(s, 1, &bit)) return false;
        code = (code << 1) | bit;

        first = t->first_code[length];
        count = t->counts[length];
        if ((count != 0) && (code >= first) && ((code - first) < count)) {
            index = (uint32_t)t->first_symbol[length] + (code - first);
            if ((index >= (uint32_t)SQZ_NC) ||
                ((int32_t)t->symbols[index] >= t->symbol_count)) {
                return false;
            }
            *symbol = t->symbols[index];
            return true;
        }
    }

    return false;
}

/* -------------------------------------------------------- bit reader --- */

static bool sqz_read_byte(sqz_state *s, uint8_t *value)
{
    if (!s || !value || s->error) return false;

    if (s->input_pos >= s->input_size) {
        /* The reference lets the stream run up to two bytes past the packed
         * extent, feeding zeroes, because SQZ.EXE's encoder flushes a partial
         * bit buffer without padding the file.  Deliberate; do not remove. */
        if (++s->padding_bytes > SQZ_MAX_PADDING_BYTES) {
            s->error = true;
            return false;
        }
        *value = 0;
        return true;
    }

    *value = s->input[s->input_pos++];
    return true;
}

static bool sqz_ensure_bits(sqz_state *s, int32_t bits)
{
    uint8_t byte;

    if (!s || s->error || (bits < 0) || (bits > SQZ_MAX_BITS)) return false;
    while (s->bit_count < bits) {
        byte = 0;
        if (!sqz_read_byte(s, &byte)) return false;
        s->bit_buffer = (s->bit_buffer << 8) | (uint32_t)byte;
        s->bit_count += 8;
    }
    return true;
}

static bool sqz_peek_bits(sqz_state *s, int32_t bits, uint32_t *value)
{
    if (!value || !sqz_ensure_bits(s, bits)) return false;
    if (bits == 0) {
        *value = 0;
        return true;
    }
    *value = (s->bit_buffer >> (s->bit_count - bits)) &
             ((1U << bits) - 1U);
    return true;
}

static bool sqz_read_bits(sqz_state *s, int32_t bits, uint32_t *value)
{
    if (!value || !sqz_peek_bits(s, bits, value)) return false;
    s->bit_count -= bits;
    if (s->bit_count == 0) {
        s->bit_buffer = 0;
    } else {
        s->bit_buffer &= (1U << s->bit_count) - 1U;
    }
    return true;
}

/* ------------------------------------------------------- table input --- */

/* Reads the 3-bit-with-unary-extension length list used for the pre-tree
 * (19 symbols) and the distance tree (31 symbols). */
static bool sqz_read_pt_lengths(sqz_state *s, int32_t symbols,
                                int32_t bit_width, int32_t special,
                                sqz_huffman *tree)
{
    uint32_t encoded;
    uint32_t constant;
    uint32_t look_ahead;
    uint32_t mask;
    uint32_t discard;
    uint32_t zeros;
    uint32_t j;
    int32_t length;
    int32_t consumed;
    int32_t i;

    if (!s || !tree || (symbols <= 0) || (symbols > SQZ_NC)) return false;
    xx_rt_memset(s->code_lengths, 0, sizeof(s->code_lengths));

    encoded = 0;
    if (!sqz_read_bits(s, bit_width, &encoded)) return false;
    if (encoded == 0) {
        constant = 0;
        return sqz_read_bits(s, bit_width, &constant) &&
               sqz_huff_set_constant(tree, constant, symbols);
    }
    if (encoded > (uint32_t)symbols) return false;

    i = 0;
    while (i < (int32_t)encoded) {
        look_ahead = 0;
        if (!sqz_peek_bits(s, 16, &look_ahead)) return false;
        length = (int32_t)(look_ahead >> 13);
        if (length == 7) {
            mask = 0x1000;
            while ((mask != 0) && ((look_ahead & mask) != 0)) {
                mask >>= 1;
                ++length;
            }
        }
        if (length > SQZ_MAX_BITS) return false;

        discard = 0;
        consumed = (length < 7) ? 3 : (length - 3);
        if (!sqz_read_bits(s, consumed, &discard)) return false;
        s->code_lengths[i++] = (uint8_t)length;

        if (i == special) {
            zeros = 0;
            if (!sqz_read_bits(s, 2, &zeros) ||
                (zeros > (encoded - (uint32_t)i))) {
                return false;
            }
            for (j = 0; j < zeros; ++j) s->code_lengths[i++] = 0;
        }
    }

    return sqz_huff_build(tree, s->code_lengths, symbols);
}

/* Reads the 511-symbol literal/length tree through the pre-tree. */
static bool sqz_read_c_lengths(sqz_state *s, const sqz_huffman *pt_tree,
                               sqz_huffman *tree)
{
    uint32_t encoded;
    uint32_t constant;
    uint32_t symbol;
    uint32_t run;
    uint32_t part;
    uint32_t length;
    int32_t i;

    if (!s || !pt_tree || !tree) return false;
    xx_rt_memset(s->code_lengths, 0, sizeof(s->code_lengths));

    encoded = 0;
    if (!sqz_read_bits(s, 9, &encoded)) return false;
    if (encoded == 0) {
        constant = 0;
        return sqz_read_bits(s, 9, &constant) &&
               sqz_huff_set_constant(tree, constant, SQZ_NC);
    }
    if (encoded > (uint32_t)SQZ_NC) return false;

    i = 0;
    while (i < (int32_t)encoded) {
        symbol = 0;
        if (!sqz_huff_decode(pt_tree, s, &symbol) ||
            (symbol >= (uint32_t)SQZ_NT)) {
            return false;
        }

        if (symbol <= 2) {
            run = 1;
            if (symbol == 1) {
                if (!sqz_read_bits(s, 4, &run)) return false;
                run += 3;
            } else if (symbol == 2) {
                run = 20;
                part = 0;
                do {
                    if (!sqz_read_bits(s, 7, &part)) return false;
                    run += part;
                    if (run > (encoded - (uint32_t)i)) return false;
                } while (part == 0x7f);
            }
            if (run > (encoded - (uint32_t)i)) return false;
            i += (int32_t)run;
        } else {
            length = symbol - 2;
            if ((length == 0) || (length > (uint32_t)SQZ_MAX_BITS)) {
                return false;
            }
            s->code_lengths[i++] = (uint8_t)length;
        }
    }

    return sqz_huff_build(tree, s->code_lengths, SQZ_NC);
}

static bool sqz_read_block(sqz_state *s)
{
    uint32_t block_size;

    block_size = 0;
    if (!sqz_read_bits(s, 14, &block_size) || (block_size == 0)) return false;

    if (!sqz_read_pt_lengths(s, SQZ_NT, 5, 3, &s->pt_tree)) return false;
    if (!sqz_read_c_lengths(s, &s->pt_tree, &s->c_tree)) return false;
    if (!sqz_read_pt_lengths(s, SQZ_NP, 5, -1, &s->p_tree)) return false;

    s->block_remaining = block_size;
    return true;
}

/* --------------------------------------------------------- decoding --- */

static bool sqz_decode_c(sqz_state *s, uint32_t *value)
{
    uint32_t symbol;
    uint32_t extra;
    int32_t index;

    if (!s || !value) return false;
    if ((s->block_remaining == 0) && !sqz_read_block(s)) return false;

    symbol = 0;
    if (!sqz_huff_decode(&s->c_tree, s, &symbol) ||
        (symbol >= (uint32_t)SQZ_NC)) {
        return false;
    }
    --s->block_remaining;

    if (symbol <= 0xff) {
        *value = symbol;
        return true;
    }

    if (s->method < 3) {
        if (symbol < 0x1c0) {
            *value = symbol;
            return true;
        }
        extra = 0;
        if (!sqz_read_bits(s, 1, &extra)) return false;
        *value = 0x1c0 + ((symbol - 0x1c0) << 1) + extra;
        return true;
    }

    if ((symbol < 0x100) || (symbol > 0x11f)) return false;
    index = (int32_t)(symbol - 0x100);
    if (sqz_length_extra[index] == 0) {
        *value = symbol;
        return true;
    }

    extra = 0;
    if (!sqz_read_bits(s, (int32_t)sqz_length_extra[index], &extra)) {
        return false;
    }
    *value = 0x100U + (uint32_t)sqz_length_base[index] + extra;
    return true;
}

static bool sqz_decode_distance(sqz_state *s, uint32_t *distance)
{
    uint32_t symbol;
    uint32_t value;
    uint32_t extra;
    int32_t extra_bits;
    int32_t index;

    if (!s || !distance) return false;
    symbol = 0;
    if (!sqz_huff_decode(&s->p_tree, s, &symbol) ||
        (symbol >= (uint32_t)SQZ_NP)) {
        return false;
    }

    value = 0;
    if ((s->method == 1) || (s->method == 3)) {
        if (symbol < 2) {
            value = symbol;
        } else {
            /* The compact table has 16 entries (symbols 0..15). */
            if (symbol > 15) return false;
            extra_bits = (int32_t)(symbol - 1);
            extra = 0;
            if (!sqz_read_bits(s, extra_bits, &extra)) return false;
            value = (1U << extra_bits) + extra;
        }
    } else {
        index = (int32_t)symbol;
        extra = 0;
        if (!sqz_read_bits(s, (int32_t)sqz_distance_extra[index], &extra)) {
            return false;
        }
        value = (uint32_t)sqz_distance_base[index] + extra;
    }

    if (value >= (uint32_t)SQZ_WINDOW_SIZE) return false;
    *distance = value;
    return true;
}

static bool sqz_emit_byte(sqz_state *s, uint8_t value)
{
    if (!s || s->error || (s->produced >= s->raw_size)) return false;
    s->window[s->window_position] = value;
    s->window_position = (s->window_position + 1) & SQZ_WINDOW_MASK;
    s->output[s->produced++] = value;
    return true;
}

/* The packed extent must be accounted for exactly, modulo the two slack bytes
 * the format tolerates (see sqz_read_byte). */
static bool sqz_finalize_input(sqz_state *s)
{
    if (!s || s->error) return false;
    return (s->input_size - s->input_pos) <= (size_t)SQZ_MAX_PADDING_BYTES;
}

/* ------------------------------------------------------------- entry --- */

bool xx_sqz_decode_memory(const uint8_t *input, size_t input_size,
                          uint32_t method, uint8_t *output,
                          size_t output_size, size_t *written)
{
    sqz_state *s;
    uint32_t code;
    uint32_t length;
    uint32_t distance;
    uint32_t copy_length;
    uint32_t i;
    int32_t source;
    bool ok;

    if (written) *written = 0;
    if ((method < 1) || (method > 4)) return false;
    if (!input && (input_size != 0)) return false;
    if (!output && (output_size != 0)) return false;

    /* A zero-length member must have a zero-length body; anything else is a
     * malformed header/body pair, exactly as the reference decides it. */
    if (output_size == 0) return input_size == 0;
    if (input_size == 0) return false;

    s = (sqz_state *)xx_mem_alloc(sizeof(sqz_state));
    if (!s) return false;
    xx_rt_memset(s, 0, sizeof(sqz_state));

    s->input = input;
    s->input_size = input_size;
    s->output = output;
    s->raw_size = output_size;
    s->method = method;

    /* SQZ.EXE primes the tail of the window with spaces, so a match at the
     * very start of a member can legitimately reference "before" the output.
     * That is load-bearing: the first 64 bytes of window history are 0x20 and
     * the rest are zero.  Do not turn this into a distance-range error. */
    xx_rt_memset(s->window + 0x7fc0, 0x20, SQZ_WINDOW_SIZE - 0x7fc0);

    while ((s->produced < s->raw_size) && !s->error) {
        code = 0;
        if (!sqz_decode_c(s, &code)) {
            s->error = true;
            break;
        }

        if (code <= 0xff) {
            if (!sqz_emit_byte(s, (uint8_t)code)) s->error = true;
            continue;
        }

        if (code <= 0xfd) {
            s->error = true;
            break;
        }
        length = code - 0xfd;
        if ((length < 3) || (length > 514)) {
            s->error = true;
            break;
        }

        distance = 0;
        if (!sqz_decode_distance(s, &distance)) {
            s->error = true;
            break;
        }
        source = (s->window_position - (int32_t)distance - 1) &
                 SQZ_WINDOW_MASK;

        /* SQZ members are terminated by their declared raw size.  The final
         * match is allowed to cross that boundary; the original extractor
         * copies only the remaining bytes.  Deliberate - clamping here is not
         * a truncation, the declared size is still required to be reached. */
        copy_length = length;
        if ((size_t)copy_length > (s->raw_size - s->produced)) {
            copy_length = (uint32_t)(s->raw_size - s->produced);
        }
        for (i = 0; i < copy_length; ++i) {
            uint8_t byte = s->window[source];
            source = (source + 1) & SQZ_WINDOW_MASK;
            if (!sqz_emit_byte(s, byte)) {
                s->error = true;
                break;
            }
        }
    }

    /* A block's token count is an upper decoding bound, not an end marker for
     * the member.  SQZ.EXE stops as soon as the declared output size has been
     * produced, so valid archives can leave tokens in the final block.  The
     * packed extent and the output size are still enforced exactly. */
    ok = !s->error && (s->produced == s->raw_size) && sqz_finalize_input(s);
    if (ok && written) *written = s->produced;

    xx_mem_free(s);
    return ok;
}

bool xx_sqz1_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           size_t *written)
{
    return xx_sqz_decode_memory(input, input_size, 1, output, output_size,
                                written);
}

bool xx_sqz2_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           size_t *written)
{
    return xx_sqz_decode_memory(input, input_size, 2, output, output_size,
                                written);
}

bool xx_sqz3_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           size_t *written)
{
    return xx_sqz_decode_memory(input, input_size, 3, output, output_size,
                                written);
}

bool xx_sqz4_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           size_t *written)
{
    return xx_sqz_decode_memory(input, input_size, 4, output, output_size,
                                written);
}
