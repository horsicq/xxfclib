/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/aldus/xx_aldus_lzsh.h"

#include <string.h>

#define LH5_MAIN_SYMBOLS 510U
#define LH5_PRE_SYMBOLS 19U
#define LH5_POSITION_SYMBOLS 14U
#define LH5_MAX_CODE_BITS 16U

typedef struct lh5_bits {
    const uint8_t *input;
    size_t input_size;
    size_t bit_offset;
} lh5_bits;

typedef struct lh5_tree {
    bool constant;
    int constant_symbol;
    uint16_t count[LH5_MAX_CODE_BITS + 1U];
    uint16_t symbols[LH5_MAIN_SYMBOLS];
    unsigned symbol_count;
} lh5_tree;

static bool lh5_get(lh5_bits *reader, unsigned bits, unsigned *value) {
    size_t bit_size, index;
    unsigned result = 0U;
    if (!reader || !value || bits > 16U ||
        reader->input_size > SIZE_MAX / 8U) return false;
    bit_size = reader->input_size * 8U;
    if (reader->bit_offset > bit_size || bits > bit_size - reader->bit_offset)
        return false;
    for (index = 0U; index < bits; ++index) {
        size_t bit = reader->bit_offset + index;
        result = (result << 1U) |
                 ((unsigned)(reader->input[bit >> 3U] >>
                            (7U - (bit & 7U))) & 1U);
    }
    reader->bit_offset += bits;
    *value = result;
    return true;
}

static void lh5_constant(lh5_tree *tree, int symbol) {
    xx_rt_memset(tree, 0, sizeof(*tree));
    tree->constant = true;
    tree->constant_symbol = symbol;
}

static bool lh5_build(lh5_tree *tree, const uint8_t *lengths,
                      unsigned length_count) {
    unsigned offsets[LH5_MAX_CODE_BITS + 1U];
    unsigned index, length, active = 0U;
    int remaining = 1;
    if (!tree || !lengths || length_count == 0U ||
        length_count > LH5_MAIN_SYMBOLS) return false;
    xx_rt_memset(tree, 0, sizeof(*tree));
    for (index = 0U; index < length_count; ++index) {
        if (lengths[index] > LH5_MAX_CODE_BITS) return false;
        if (lengths[index] != 0U) {
            ++tree->count[lengths[index]];
            ++active;
        }
    }
    if (active == 0U) return false;
    for (length = 1U; length <= LH5_MAX_CODE_BITS; ++length) {
        remaining = (remaining << 1) - tree->count[length];
        if (remaining < 0) return false;
    }
    offsets[0] = 0U;
    offsets[1] = 0U;
    for (length = 1U; length < LH5_MAX_CODE_BITS; ++length)
        offsets[length + 1U] = offsets[length] + tree->count[length];
    for (index = 0U; index < length_count; ++index) {
        length = lengths[index];
        if (length != 0U) tree->symbols[offsets[length]++] = (uint16_t)index;
    }
    tree->symbol_count = active;
    return true;
}

static int lh5_symbol(lh5_bits *reader, const lh5_tree *tree) {
    unsigned code = 0U, first = 0U, position = 0U, length;
    if (!reader || !tree) return -1;
    if (tree->constant) return tree->constant_symbol;
    for (length = 1U; length <= LH5_MAX_CODE_BITS; ++length) {
        unsigned bit, amount, at;
        if (!lh5_get(reader, 1U, &bit)) return -1;
        code = (code << 1U) | bit;
        amount = tree->count[length];
        if (code >= first && code - first < amount) {
            at = position + code - first;
            return at < tree->symbol_count ? tree->symbols[at] : -1;
        }
        position += amount;
        first = (first + amount) << 1U;
    }
    return -1;
}

static bool lh5_read_pt_lengths(lh5_bits *reader, unsigned symbols,
                                unsigned count_bits, int special,
                                lh5_tree *tree) {
    uint8_t lengths[LH5_PRE_SYMBOLS];
    unsigned entries, index = 0U;
    if (!reader || !tree || symbols > sizeof(lengths) ||
        !lh5_get(reader, count_bits, &entries)) return false;
    if (entries == 0U) {
        unsigned symbol;
        if (!lh5_get(reader, count_bits, &symbol) || symbol >= symbols)
            return false;
        lh5_constant(tree, (int)symbol);
        return true;
    }
    if (entries > symbols) return false;
    xx_rt_memset(lengths, 0, sizeof(lengths));
    while (index < entries) {
        unsigned length;
        if (!lh5_get(reader, 3U, &length)) return false;
        if (length == 7U) {
            unsigned bit;
            do {
                if (!lh5_get(reader, 1U, &bit)) return false;
                if (bit != 0U && ++length > LH5_MAX_CODE_BITS) return false;
            } while (bit != 0U);
        }
        lengths[index++] = (uint8_t)length;
        if (special >= 0 && index == (unsigned)special) {
            unsigned skip;
            if (!lh5_get(reader, 2U, &skip) || skip > symbols - index)
                return false;
            while (skip--) lengths[index++] = 0U;
        }
    }
    return lh5_build(tree, lengths, symbols);
}

static bool lh5_read_main_lengths(lh5_bits *reader, const lh5_tree *pre,
                                  lh5_tree *tree) {
    uint8_t lengths[LH5_MAIN_SYMBOLS];
    unsigned entries;
    unsigned index = 0U;
    if (!reader || !pre || !tree || !lh5_get(reader, 9U, &entries)) return false;
    if (entries == 0U) {
        unsigned symbol;
        if (!lh5_get(reader, 9U, &symbol) || symbol >= LH5_MAIN_SYMBOLS)
            return false;
        lh5_constant(tree, (int)symbol);
        return true;
    }
    xx_rt_memset(lengths, 0, sizeof(lengths));
    while (index < entries) {
        int symbol = lh5_symbol(reader, pre);
        unsigned run = 1U;
        if (symbol < 0) return false;
        if (symbol <= 2) {
            unsigned value;
            if (symbol == 1) {
                if (!lh5_get(reader, 4U, &value)) return false;
                run = value + 3U;
            } else if (symbol == 2) {
                if (!lh5_get(reader, 9U, &value)) return false;
                run = value + 20U;
            }
            while (run--) {
                if (index < LH5_MAIN_SYMBOLS) lengths[index] = 0U;
                ++index;
            }
        } else {
            if ((unsigned)(symbol - 2) > LH5_MAX_CODE_BITS) return false;
            if (index < LH5_MAIN_SYMBOLS) lengths[index] = (uint8_t)(symbol - 2);
            ++index;
        }
        if (index > LH5_MAIN_SYMBOLS + 531U) return false;
    }
    return lh5_build(tree, lengths, LH5_MAIN_SYMBOLS);
}

static bool lh5_decode(const uint8_t *input, size_t input_size,
                       uint8_t *output, size_t output_size) {
    lh5_bits reader;
    lh5_tree literal_tree, position_tree;
    unsigned symbols_left = 0U;
    size_t output_at = 0U;
    if (!input || !output || output_size == 0U) return false;
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.input = input;
    reader.input_size = input_size;
    while (output_at < output_size) {
        if (symbols_left == 0U) {
            lh5_tree pre_tree;
            if (!lh5_get(&reader, 16U, &symbols_left) || symbols_left == 0U ||
                !lh5_read_pt_lengths(&reader, LH5_PRE_SYMBOLS, 5U, 3,
                                     &pre_tree) ||
                !lh5_read_main_lengths(&reader, &pre_tree, &literal_tree) ||
                !lh5_read_pt_lengths(&reader, LH5_POSITION_SYMBOLS, 4U, -1,
                                     &position_tree)) return false;
        }
        {
            int symbol;
            --symbols_left;
            symbol = lh5_symbol(&reader, &literal_tree);
            if (symbol < 0) return false;
            if (symbol < 256) {
                output[output_at++] = (uint8_t)symbol;
                continue;
            }
            {
                unsigned length = (unsigned)(symbol - 256 + 3);
                int distance_code = lh5_symbol(&reader, &position_tree);
                unsigned base, extra = 0U;
                size_t distance, index;
                if (distance_code < 0 || distance_code > LH5_MAX_CODE_BITS ||
                    (distance_code > 0 &&
                     !lh5_get(&reader, (unsigned)distance_code - 1U, &extra)))
                    return false;
                base = distance_code == 0 ? 0U
                                          : (1U << ((unsigned)distance_code - 1U));
                distance = (size_t)base + extra + 1U;
                if (distance > output_at || length > output_size - output_at)
                    return false;
                for (index = 0U; index < length; ++index)
                    output[output_at++] = output[output_at - distance];
            }
        }
    }
    return true;
}

static uint16_t crc16_arc(const uint8_t *data, size_t size) {
    uint16_t crc = 0U;
    size_t index;
    for (index = 0U; index < size; ++index) {
        unsigned bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 1U) ? (uint16_t)((crc >> 1U) ^ 0xa001U)
                              : (uint16_t)(crc >> 1U);
    }
    return crc;
}

bool xx_aldus_lzsh_decode_block(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_size,
                                 size_t *written) {
    uint16_t expected_crc;
    bool decoded;
    if (written) *written = 0U;
    if (!input || !output || input_size < 3U || output_size == 0U) return false;
    expected_crc = (uint16_t)(input[0] | ((uint16_t)input[1] << 8U));
    if (input[2] == 0U) {
        if (input_size - 3U < output_size) return false;
        xx_rt_memcpy(output, input + 3U, output_size);
        decoded = true;
    } else if (input[2] == 1U) {
        decoded = lh5_decode(input + 3U, input_size - 3U, output, output_size);
    } else {
        return false;
    }
    if (!decoded || crc16_arc(output, output_size) != expected_crc) return false;
    if (written) *written = output_size;
    return true;
}
