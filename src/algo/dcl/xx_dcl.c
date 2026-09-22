/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * A native implementation of the public PKWARE DCL bitstream specification.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <string.h>

#define DCL_MAX_BITS 13U

typedef struct dcl_bits {
    const uint8_t *input;
    size_t input_size;
    size_t offset;
    uint32_t bits;
    unsigned count;
} dcl_bits;

typedef struct dcl_huffman {
    uint16_t count[DCL_MAX_BITS + 1U];
    uint16_t symbol[256];
    unsigned symbols;
} dcl_huffman;

/* The DCL specification stores its fixed trees as runs: high nibble is
 * repeat-count minus one and low nibble is a code length. */
static const uint8_t dcl_literal_runs[] = {
    11,124,8,7,28,7,188,13,76,4,10,8,12,10,12,10,8,23,8,
    9,7,6,7,8,7,6,55,8,23,24,12,11,7,9,11,12,6,7,22,5,
    7,24,6,11,9,6,7,22,7,11,38,7,9,8,25,11,8,11,9,12,
    8,12,5,38,5,38,5,11,7,5,6,21,6,10,53,8,7,24,10,27,
    44,253,253,253,252,252,252,13,12,45,12,45,12,61,12,45,
    44,173
};
static const uint8_t dcl_length_runs[] = { 2,35,36,53,38,23 };
static const uint8_t dcl_distance_runs[] = { 2,20,53,230,247,151,248 };
static const uint16_t dcl_length_base[16] = {
    3,2,4,5,6,7,8,9,10,12,16,24,40,72,136,264
};
static const uint8_t dcl_length_extra[16] = {
    0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8
};

static bool dcl_get_bits(dcl_bits *reader, unsigned need, unsigned *value) {
    uint32_t mask;
    if (!reader || !value || need > 16U) return false;
    while (reader->count < need) {
        if (reader->offset >= reader->input_size) return false;
        reader->bits |= (uint32_t)reader->input[reader->offset++] <<
                        reader->count;
        reader->count += 8U;
    }
    mask = need == 0U ? 0U : ((1U << need) - 1U);
    *value = reader->bits & mask;
    reader->bits >>= need;
    reader->count -= need;
    return true;
}

static bool dcl_build_tree(dcl_huffman *tree, const uint8_t *runs,
                           size_t run_count) {
    uint8_t lengths[256];
    unsigned offsets[DCL_MAX_BITS + 1U];
    unsigned symbol_count = 0U;
    unsigned index, length;
    int free_codes = 1;
    if (!tree || !runs || run_count == 0U) return false;
    xx_rt_memset(tree, 0, sizeof(*tree));
    for (index = 0U; index < run_count; ++index) {
        unsigned repeat = ((unsigned)runs[index] >> 4U) + 1U;
        length = (unsigned)runs[index] & 15U;
        if (length > DCL_MAX_BITS || repeat > sizeof(lengths) - symbol_count)
            return false;
        while (repeat--) lengths[symbol_count++] = (uint8_t)length;
    }
    if (symbol_count == 0U) return false;
    for (index = 0U; index < symbol_count; ++index) {
        if (lengths[index] == 0U) return false;
        ++tree->count[lengths[index]];
    }
    for (length = 1U; length <= DCL_MAX_BITS; ++length) {
        free_codes = (free_codes << 1) - tree->count[length];
        if (free_codes < 0) return false;
    }
    offsets[0] = 0U;
    offsets[1] = 0U;
    for (length = 1U; length < DCL_MAX_BITS; ++length)
        offsets[length + 1U] = offsets[length] + tree->count[length];
    for (index = 0U; index < symbol_count; ++index) {
        length = lengths[index];
        tree->symbol[offsets[length]++] = (uint16_t)index;
    }
    tree->symbols = symbol_count;
    return true;
}

/* DCL transmits canonical codes LSB-first and inverted.  Accumulating the
 * inverse bits makes the normal canonical bounds test work directly. */
static int dcl_symbol(dcl_bits *reader, const dcl_huffman *tree) {
    unsigned code = 0U, first = 0U, position = 0U, length;
    if (!reader || !tree) return -1;
    for (length = 1U; length <= DCL_MAX_BITS; ++length) {
        unsigned bit;
        if (!dcl_get_bits(reader, 1U, &bit)) return -1;
        code |= bit ^ 1U;
        if (code < first + tree->count[length]) {
            unsigned at = position + code - first;
            return at < tree->symbols ? tree->symbol[at] : -1;
        }
        position += tree->count[length];
        first = (first + tree->count[length]) << 1U;
        code <<= 1U;
    }
    return -1;
}

/*
 * The decode loop, shared by the two entry points below.
 *
 * With @p output non-NULL the bytes go there and @p limit is the exact size
 * expected. With @p output NULL nothing is kept but a sliding window, so the
 * stream can be measured without knowing its size in advance and without
 * allocating for it -- which is what a reader needs when the container stores
 * no uncompressed size at all.
 *
 * The window is the reason measuring is cheap: a match reaches back at most
 * (63 << 6) + 63 + 1 bytes, so anything older can be forgotten.
 */
#define DCL_WINDOW_SIZE 8192U

static bool dcl_run(const uint8_t *input, size_t input_size, uint8_t *output,
                    size_t limit, size_t *produced, size_t *consumed) {
    dcl_bits reader;
    dcl_huffman literals, lengths, distances;
    unsigned literal_mode, dictionary_bits;
    uint8_t window[DCL_WINDOW_SIZE];
    size_t output_at = 0U;
    bool ended = false;

    if (produced) *produced = 0U;
    if (consumed) *consumed = 0U;
    if (!input || input_size < 3U || limit == 0U) return false;
    if (!output) xx_rt_memset(window, 0, sizeof(window));

    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.input = input;
    reader.input_size = input_size;
    if (!dcl_build_tree(&literals, dcl_literal_runs,
                        sizeof(dcl_literal_runs)) ||
        !dcl_build_tree(&lengths, dcl_length_runs,
                        sizeof(dcl_length_runs)) ||
        !dcl_build_tree(&distances, dcl_distance_runs,
                        sizeof(dcl_distance_runs)) ||
        !dcl_get_bits(&reader, 8U, &literal_mode) ||
        !dcl_get_bits(&reader, 8U, &dictionary_bits) || literal_mode > 1U ||
        dictionary_bits < 4U || dictionary_bits > 6U) {
        return false;
    }

    while (!ended) {
        unsigned is_match;
        if (!dcl_get_bits(&reader, 1U, &is_match)) return false;
        if (is_match == 0U) {
            int literal;
            unsigned value;
            if (literal_mode == 0U) {
                if (!dcl_get_bits(&reader, 8U, &value)) return false;
                literal = (int)value;
            } else {
                literal = dcl_symbol(&reader, &literals);
            }
            if (literal < 0 || output_at >= limit) return false;
            if (output) {
                output[output_at] = (uint8_t)literal;
            } else {
                window[output_at % DCL_WINDOW_SIZE] = (uint8_t)literal;
            }
            ++output_at;
            continue;
        }
        {
            int length_symbol = dcl_symbol(&reader, &lengths);
            unsigned extra, length, distance_bits, distance_extra;
            int distance_symbol;
            size_t distance, copy;
            if (length_symbol < 0 || length_symbol >= 16 ||
                !dcl_get_bits(&reader, dcl_length_extra[length_symbol],
                              &extra)) {
                return false;
            }
            length = dcl_length_base[length_symbol] + extra;
            if (length == 519U) {
                ended = true;
                continue;
            }
            distance_bits = length == 2U ? 2U : dictionary_bits;
            distance_symbol = dcl_symbol(&reader, &distances);
            if (distance_symbol < 0 ||
                !dcl_get_bits(&reader, distance_bits, &distance_extra)) {
                return false;
            }
            distance = ((size_t)distance_symbol << distance_bits) +
                       distance_extra + 1U;
            if (distance > output_at || length > limit - output_at) {
                return false;
            }
            /* A match may overlap its own output, so this copies one byte at
             * a time rather than in a block. */
            for (copy = 0U; copy < length; ++copy) {
                if (output) {
                    output[output_at] = output[output_at - distance];
                } else {
                    window[output_at % DCL_WINDOW_SIZE] =
                        window[(output_at - distance) % DCL_WINDOW_SIZE];
                }
                ++output_at;
            }
        }
    }

    if (produced) *produced = output_at;
    /* Bytes taken from the input, not counting whole bytes still sitting
     * unread in the bit cache: a caller checking that a stream ends exactly
     * at EOF needs the position the decoder actually reached. */
    if (consumed) {
        size_t whole_bytes_buffered = reader.count / 8U;
        *consumed = reader.offset > whole_bytes_buffered
                        ? reader.offset - whole_bytes_buffered
                        : 0U;
    }
    return true;
}

bool xx_dcl_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written) {
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!output) return false;
    if (!dcl_run(input, input_size, output, output_size, &produced, NULL)) {
        return false;
    }
    if (written) *written = produced;
    return produced == output_size;
}

bool xx_dcl_scan_memory(const uint8_t *input, size_t input_size,
                        size_t max_output, size_t *consumed,
                        size_t *produced) {
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (max_output == 0U) return false;
    return dcl_run(input, input_size, NULL, max_output, produced, consumed);
}
