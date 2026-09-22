/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FoxPro FPAK member codec (PKZIP Implode).  Ported from
 * XArchive/Algos/xfpakdecoder.cpp - the tree reader, the complemented
 * canonical code walk and every validity test below are that decoder's.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/fpak/xx_fpak.h"

/* Both ceilings are the reference's: it refuses a member larger than 512 MiB
 * and one that does not fit a signed 32-bit size. */
#define FPAK_MAX_OUTPUT ((uint64_t)512U * 1024U * 1024U)
#define FPAK_MAX_INT32 ((uint64_t)0x7fffffffU)
/* The literal tree is the widest of the three; length and distance are 64. */
#define FPAK_MAX_TREE_SYMBOLS 256
#define FPAK_CODE_TREE_SYMBOLS 64

typedef struct fpak_bits {
    const uint8_t *data;
    uint64_t total_bits;
    uint64_t bit_position;
    /* Set when a read was refused because the stream had too few bits left.
     * It is the only thing that separates "the member continues on the next
     * volume" from "this stream is malformed", and a multi-bit read refuses
     * WITHOUT advancing the position, so the position alone cannot say. */
    bool ran_out;
} fpak_bits;

typedef struct fpak_tree {
    uint32_t count[17];
    uint32_t first_code[17];
    int32_t first_symbol[17];
    uint8_t symbols[FPAK_MAX_TREE_SYMBOLS];
    int32_t symbol_count;
} fpak_tree;

static bool fpak_read_bit(fpak_bits *bits, uint32_t *value)
{
    if (!value) return false;
    if (bits->bit_position >= bits->total_bits) {
        bits->ran_out = true;
        return false;
    }
    *value = (uint32_t)((bits->data[bits->bit_position >> 3] >>
                         (bits->bit_position & 7U)) &
                        1U);
    ++bits->bit_position;
    return true;
}

static bool fpak_read_bits(fpak_bits *bits, uint32_t count, uint32_t *value)
{
    uint32_t result = 0U;
    uint32_t i;
    if (!value || (count > 24U)) return false;
    if (bits->bit_position + count > bits->total_bits) {
        bits->ran_out = true;
        return false;
    }
    for (i = 0U; i < count; ++i) {
        uint32_t bit = 0U;
        if (!fpak_read_bit(bits, &bit)) return false;
        result |= bit << i;
    }
    *value = result;
    return true;
}

static uint64_t fpak_consumed(const fpak_bits *bits)
{
    return (bits->bit_position + 7U) / 8U;
}

/* symbol_total is 64 for the length and distance trees and 256 for the
 * literal tree; the run-length encoding of the code lengths is identical. */
static bool fpak_read_tree(const uint8_t *packed, size_t packed_size,
                           size_t *position, int32_t symbol_total,
                           fpak_tree *tree)
{
    uint8_t lengths[FPAK_MAX_TREE_SYMBOLS];
    uint32_t pair_count;
    uint32_t i;
    uint32_t code;
    uint32_t length;
    int32_t symbol_count = 0;
    int32_t output_index = 0;
    int32_t symbol;
    uint32_t kraft_units = 0U;

    if (!position || !tree || (*position >= packed_size) ||
        (symbol_total < 1) || (symbol_total > FPAK_MAX_TREE_SYMBOLS))
        return false;

    xx_rt_memset(lengths, 0, sizeof(lengths));
    pair_count = (uint32_t)packed[(*position)++] + 1U;
    /* Every pair describes at least one of the tree's symbols. */
    if (pair_count > (uint32_t)symbol_total) return false;

    for (i = 0U; i < pair_count; ++i) {
        uint8_t descriptor;
        int32_t repeat;
        uint8_t code_length;
        if (*position >= packed_size) return false;
        descriptor = packed[(*position)++];
        repeat = (int32_t)(descriptor >> 4) + 1;
        code_length = (uint8_t)((descriptor & 0x0fU) + 1U);
        if ((symbol_count > symbol_total - repeat) || (code_length > 16U))
            return false;
        for (; repeat > 0; --repeat) lengths[symbol_count++] = code_length;
    }
    if (symbol_count != symbol_total) return false;

    xx_rt_memset(tree, 0, sizeof(*tree));
    tree->symbol_count = symbol_total;
    for (symbol = 0; symbol < symbol_total; ++symbol) {
        ++tree->count[lengths[symbol]];
        kraft_units += 1U << (16U - lengths[symbol]);
    }
    /* Implode stores complete Shannon-Fano trees.  Reject over-subscribed and
     * incomplete descriptions alike; accepting either makes random data look
     * like a usable member and can hide a truncated table. */
    if (kraft_units != (1U << 16)) return false;

    code = 0U;
    for (length = 1U; length <= 16U; ++length) {
        code = (code + tree->count[length - 1U]) << 1;
        tree->first_code[length] = code;
        tree->first_symbol[length] = output_index;
        for (symbol = 0; symbol < symbol_total; ++symbol) {
            if (lengths[symbol] == length)
                tree->symbols[output_index++] = (uint8_t)symbol;
        }
    }
    /* `code` is first_code[16] here: the reference checks the walk closed on
     * the whole 16-bit space, deliberately after the loop. */
    return (output_index == symbol_total) &&
           (code + tree->count[16] == (1U << 16));
}

static bool fpak_decode_symbol(fpak_bits *bits, const fpak_tree *tree,
                               uint32_t *symbol)
{
    uint32_t code = 0U;
    uint32_t length;
    if (!symbol) return false;
    for (length = 1U; length <= 16U; ++length) {
        uint32_t bit = 0U;
        uint32_t first;
        uint32_t count;
        if (!fpak_read_bit(bits, &bit)) return false;
        /* Implode transmits complemented canonical codes least-significant
         * bit first.  Complementing the incoming bit while growing an MSB-
         * first prefix lets us use the ordinary canonical-code ranges. */
        code = (code << 1) | (bit ^ 1U);
        first = tree->first_code[length];
        count = tree->count[length];
        if (count && (code >= first) && ((code - first) < count)) {
            const int32_t index =
                tree->first_symbol[length] + (int32_t)(code - first);
            if ((index < 0) || (index >= tree->symbol_count)) return false;
            *symbol = tree->symbols[index];
            return true;
        }
    }
    return false;
}

static bool fpak_decode_core(const uint8_t *input, size_t input_size,
                             uint16_t method, uint16_t flags,
                             uint8_t *output, size_t output_size,
                             size_t *written, bool allow_partial)
{
    fpak_tree literal_tree;
    fpak_tree length_tree;
    fpak_tree distance_tree;
    fpak_bits bits;
    size_t position = 0U;
    size_t produced = 0U;
    bool use_literal_tree;
    uint32_t distance_bits;
    uint32_t minimum_match;
    uint32_t dictionary_size;

    if (written) *written = 0U;
    if ((!input && input_size) || (!output && output_size)) return false;
    if ((input_size == 0U) || (output_size < 1U) ||
        ((uint64_t)output_size > FPAK_MAX_OUTPUT) ||
        ((uint64_t)output_size > FPAK_MAX_INT32))
        return false;

    if (method == XX_FPAK_METHOD_STORED) {
        /* A stored member is its own payload; the segment chain has already
         * been concatenated by the caller, so the two sizes must agree -
         * unless a partial decode was asked for, in which case the slice this
         * volume holds is the member's prefix. */
        if (input_size != output_size) {
            if (!allow_partial || input_size > output_size) return false;
        }
        xx_rt_memcpy(output, input, input_size);
        if (written) *written = input_size;
        return true;
    }
    if (method != XX_FPAK_METHOD_IMPLODED) return false;

    /* 0x08 is the data-descriptor bit and has no effect on the bit stream. */
    use_literal_tree = (flags & XX_FPAK_FLAG_LITERAL_TREE) != 0U;
    distance_bits = (flags & XX_FPAK_FLAG_DICTIONARY_8K) ? 7U : 6U;
    minimum_match = use_literal_tree ? 3U : 2U;
    dictionary_size = 1U << (distance_bits + 6U);

    if (use_literal_tree &&
        !fpak_read_tree(input, input_size, &position, FPAK_MAX_TREE_SYMBOLS,
                        &literal_tree))
        return false;
    if (!fpak_read_tree(input, input_size, &position, FPAK_CODE_TREE_SYMBOLS,
                        &length_tree) ||
        !fpak_read_tree(input, input_size, &position, FPAK_CODE_TREE_SYMBOLS,
                        &distance_tree) ||
        (position >= input_size))
        return false;

    bits.data = input;
    bits.total_bits = (uint64_t)input_size * 8U;
    bits.bit_position = (uint64_t)position * 8U;
    bits.ran_out = false;

    while (produced < output_size) {
        uint32_t literal_flag = 0U;
        if (!fpak_read_bit(&bits, &literal_flag)) goto stopped;
        if (literal_flag) {
            uint32_t literal = 0U;
            if (use_literal_tree) {
                if (!fpak_decode_symbol(&bits, &literal_tree, &literal))
                    goto stopped;
            } else if (!fpak_read_bits(&bits, 8U, &literal)) {
                goto stopped;
            }
            output[produced++] = (uint8_t)literal;
        } else {
            uint32_t low_distance = 0U;
            uint32_t distance_symbol = 0U;
            uint32_t length_symbol = 0U;
            uint32_t length;
            uint32_t distance;
            uint32_t i;
            if (!fpak_read_bits(&bits, distance_bits, &low_distance) ||
                !fpak_decode_symbol(&bits, &distance_tree, &distance_symbol) ||
                !fpak_decode_symbol(&bits, &length_tree, &length_symbol))
                goto stopped;
            length = length_symbol + minimum_match;
            if (length_symbol == 63U) {
                uint32_t extra_length = 0U;
                if (!fpak_read_bits(&bits, 8U, &extra_length)) goto stopped;
                length += extra_length;
            }
            distance = (distance_symbol << distance_bits) + low_distance + 1U;
            if (!distance || (distance > dictionary_size) ||
                ((uint64_t)length > (uint64_t)(output_size - produced)))
                return false;
            /* PKZIP Implode starts with a zero-filled dictionary.  A legal
             * early match may therefore point before the first produced byte;
             * this is NOT a malformed back-reference and must not be made one.
             * Copying one byte at a time also gives overlapping LZ copies. */
            for (i = 0U; i < length; ++i) {
                const uint8_t value = (size_t)distance <= produced
                                          ? output[produced - distance]
                                          : (uint8_t)0;
                output[produced++] = value;
            }
        }
    }

    /* The stream must end exactly at the end of the member: a short read is a
     * desync and trailing bytes mean the wrong profile was used. */
    if ((produced != output_size) || (fpak_consumed(&bits) != input_size))
        return false;

    if (written) *written = produced;
    return true;

stopped:
    /* A slice of a member that continues on the next volume runs out of bits
     * mid-token.  The plaintext produced up to that point is the true prefix
     * of the member - LZ decoding is prefix-correct - so a caller that asked
     * for a partial decode gets it.  Anything else is a desync, and a desync
     * is still a failure. */
    if (allow_partial && bits.ran_out && produced != 0U) {
        if (written) *written = produced;
        return true;
    }
    return false;
}

bool xx_fpak_decode_memory_profile(const uint8_t *input, size_t input_size,
                                   uint16_t method, uint16_t flags,
                                   uint8_t *output, size_t output_size,
                                   size_t *written)
{
    return fpak_decode_core(input, input_size, method, flags, output,
                            output_size, written, false);
}

bool xx_fpak_decode_partial_profile(const uint8_t *input, size_t input_size,
                                    uint16_t method, uint16_t flags,
                                    uint8_t *output, size_t output_size,
                                    size_t *written)
{
    return fpak_decode_core(input, input_size, method, flags, output,
                            output_size, written, true);
}

bool xx_fpak_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           size_t *written)
{
    return xx_fpak_decode_memory_profile(input, input_size,
                                         XX_FPAK_METHOD_IMPLODED, 0U, output,
                                         output_size, written);
}
