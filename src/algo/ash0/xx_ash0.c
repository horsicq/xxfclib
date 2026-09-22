/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Nintendo ASH0 uses independent, MSB-first serialized binary trees for its
 * literal/length and distance streams.  This is a native, bounds-checked
 * implementation of that documented transport.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/ash0/xx_ash0.h"

#include <stdlib.h>
#include <string.h>

#define XX_ASH0_HEADER_SIZE 12U
#define XX_ASH0_MIN_SIZE 20U
#define XX_ASH0_MIN_DISTANCE_OFFSET 16U
#define XX_ASH0_WORD_SIZE 4U
#define XX_ASH0_SYMBOL_BITS 9U
#define XX_ASH0_LITERAL_LIMIT 256U
#define XX_ASH0_MIN_MATCH 3U
#define XX_ASH0_MAX_UNCOMPRESSED UINT32_C(0x00ffffff)

typedef struct xx_ash0_bit_reader_s {
    const uint8_t *data;
    size_t start;
    size_t end;
    size_t bit_position;
} xx_ash0_bit_reader;

typedef struct xx_ash0_tree_s {
    uint32_t *left;
    uint32_t *right;
    uint32_t *pending;
    uint8_t *seen;
    uint32_t maximum_leaf;
    uint32_t table_size;
    uint32_t root;
} xx_ash0_tree;

static uint32_t xx_ash0_read32be(const uint8_t *data) {
    return ((uint32_t)data[0] << 24U) | ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) | (uint32_t)data[3];
}

bool xx_ash0_parse_header(const uint8_t *input, size_t input_size,
                          xx_ash0_header *header) {
    uint32_t size_word;
    uint32_t distance_offset;
    if (!input || !header || input_size < XX_ASH0_MIN_SIZE ||
        input[0] != 'A' || input[1] != 'S' || input[2] != 'H' ||
        input[3] != '0') {
        return false;
    }
    size_word = xx_ash0_read32be(input + 4U);
    distance_offset = xx_ash0_read32be(input + 8U);
    if ((size_word & XX_ASH0_MAX_UNCOMPRESSED) == 0U ||
        distance_offset < XX_ASH0_MIN_DISTANCE_OFFSET ||
        distance_offset > input_size - XX_ASH0_WORD_SIZE) {
        return false;
    }
    header->uncompressed_size = size_word & XX_ASH0_MAX_UNCOMPRESSED;
    header->distance_offset = distance_offset;
    header->size_word_top_byte = (uint8_t)(size_word >> 24U);
    return true;
}

static bool xx_ash0_bit_reader_init(xx_ash0_bit_reader *reader,
                                    const uint8_t *data, size_t start,
                                    size_t end) {
    if (!reader || !data || start > end || end - start < XX_ASH0_WORD_SIZE) {
        return false;
    }
    reader->data = data;
    reader->start = start;
    reader->end = end;
    reader->bit_position = 0U;
    return true;
}

static bool xx_ash0_read_bit(xx_ash0_bit_reader *reader, uint32_t *value) {
    size_t byte_position;
    if (!reader || !value) return false;
    byte_position = reader->bit_position >> 3U;
    if (byte_position >= reader->end - reader->start) return false;
    *value = (uint32_t)((reader->data[reader->start + byte_position] >>
                         (7U - (reader->bit_position & 7U))) & 1U);
    ++reader->bit_position;
    return true;
}

static bool xx_ash0_read_bits(xx_ash0_bit_reader *reader, unsigned count,
                              uint32_t *value) {
    uint32_t result = 0U;
    unsigned index;
    if (!reader || !value || count == 0U || count > 31U) return false;
    for (index = 0U; index < count; ++index) {
        uint32_t bit;
        if (!xx_ash0_read_bit(reader, &bit)) return false;
        result = (result << 1U) | bit;
    }
    *value = result;
    return true;
}

static void xx_ash0_tree_cleanup(xx_ash0_tree *tree) {
    if (!tree) return;
    xx_rt_free(tree->seen);
    xx_rt_free(tree->pending);
    xx_rt_free(tree->right);
    xx_rt_free(tree->left);
    xx_rt_memset(tree, 0, sizeof(*tree));
}

static bool xx_ash0_tree_init(xx_ash0_tree *tree, unsigned width) {
    uint32_t leaves;
    uint32_t table_size;
    if (!tree || width < 1U || width > 15U) return false;
    xx_rt_memset(tree, 0, sizeof(*tree));
    leaves = UINT32_C(1) << width;
    table_size = leaves * 2U - 1U;
    tree->left = (uint32_t *)xx_rt_malloc((size_t)table_size * sizeof(*tree->left));
    tree->right = (uint32_t *)xx_rt_malloc((size_t)table_size * sizeof(*tree->right));
    tree->pending = (uint32_t *)xx_rt_malloc((size_t)table_size * sizeof(*tree->pending));
    tree->seen = (uint8_t *)xx_rt_calloc((size_t)leaves, sizeof(*tree->seen));
    if (!tree->left || !tree->right || !tree->pending || !tree->seen) {
        xx_ash0_tree_cleanup(tree);
        return false;
    }
    tree->maximum_leaf = leaves;
    tree->table_size = table_size;
    tree->root = leaves;
    return true;
}

static bool xx_ash0_tree_attach(xx_ash0_tree *tree, uint32_t pending,
                                uint32_t child) {
    uint32_t node;
    if (!tree) return false;
    node = pending >> 1U;
    if (node < tree->maximum_leaf || node >= tree->table_size ||
        child >= tree->table_size) {
        return false;
    }
    if ((pending & 1U) != 0U) tree->right[node] = child;
    else tree->left[node] = child;
    return true;
}

/* Tree serialization is pre-order: an internal node is bit 1, a leaf is bit
 * 0 followed by its fixed-width value.  The root must be internal. */
static bool xx_ash0_read_tree(xx_ash0_bit_reader *reader, unsigned width,
                              xx_ash0_tree *tree) {
    uint32_t first;
    uint32_t next_node;
    size_t pending_count = 0U;
    if (!reader || !tree || !xx_ash0_tree_init(tree, width) ||
        !xx_ash0_read_bit(reader, &first) || first == 0U) {
        xx_ash0_tree_cleanup(tree);
        return false;
    }
    next_node = tree->root + 1U;
    tree->pending[pending_count++] = (tree->root << 1U) | 1U;
    tree->pending[pending_count++] = tree->root << 1U;
    while (pending_count != 0U) {
        uint32_t pending = tree->pending[--pending_count];
        uint32_t marker;
        if (!xx_ash0_read_bit(reader, &marker)) {
            xx_ash0_tree_cleanup(tree);
            return false;
        }
        if (marker != 0U) {
            uint32_t child;
            if (next_node >= tree->table_size ||
                pending_count > (size_t)tree->table_size - 2U) {
                xx_ash0_tree_cleanup(tree);
                return false;
            }
            child = next_node++;
            if (!xx_ash0_tree_attach(tree, pending, child)) {
                xx_ash0_tree_cleanup(tree);
                return false;
            }
            tree->pending[pending_count++] = (child << 1U) | 1U;
            tree->pending[pending_count++] = child << 1U;
        } else {
            uint32_t value;
            if (!xx_ash0_read_bits(reader, width, &value) ||
                value >= tree->maximum_leaf || tree->seen[value] != 0U ||
                !xx_ash0_tree_attach(tree, pending, value)) {
                xx_ash0_tree_cleanup(tree);
                return false;
            }
            tree->seen[value] = 1U;
        }
    }
    return true;
}

static bool xx_ash0_read_code(xx_ash0_bit_reader *reader,
                              const xx_ash0_tree *tree, uint32_t *value) {
    uint32_t node;
    uint32_t guard = 0U;
    if (!reader || !tree || !value) return false;
    node = tree->root;
    while (node >= tree->maximum_leaf) {
        uint32_t bit;
        if (node >= tree->table_size || ++guard > tree->table_size ||
            !xx_ash0_read_bit(reader, &bit)) {
            return false;
        }
        node = bit != 0U ? tree->right[node] : tree->left[node];
    }
    *value = node;
    return true;
}

static bool xx_ash0_decode_attempt(const uint8_t *input, size_t input_size,
                                   const xx_ash0_header *header,
                                   uint8_t *output, size_t output_size,
                                   unsigned distance_bits, bool *tight) {
    xx_ash0_bit_reader symbol_reader;
    xx_ash0_bit_reader distance_reader;
    xx_ash0_tree symbol_tree;
    xx_ash0_tree distance_tree;
    size_t output_position = 0U;
    bool result = false;
    if (!input || !header || !output || !tight ||
        output_size != (size_t)header->uncompressed_size ||
        (distance_bits != 11U && distance_bits != 15U) ||
        !xx_ash0_bit_reader_init(&symbol_reader, input, XX_ASH0_HEADER_SIZE,
                                 (size_t)header->distance_offset) ||
        !xx_ash0_bit_reader_init(&distance_reader, input,
                                 (size_t)header->distance_offset,
                                 input_size)) {
        return false;
    }
    xx_rt_memset(&symbol_tree, 0, sizeof(symbol_tree));
    xx_rt_memset(&distance_tree, 0, sizeof(distance_tree));
    if (!xx_ash0_read_tree(&symbol_reader, XX_ASH0_SYMBOL_BITS, &symbol_tree) ||
        !xx_ash0_read_tree(&distance_reader, distance_bits, &distance_tree)) {
        goto cleanup;
    }
    while (output_position < output_size) {
        uint32_t symbol;
        if (!xx_ash0_read_code(&symbol_reader, &symbol_tree, &symbol)) {
            goto cleanup;
        }
        if (symbol < XX_ASH0_LITERAL_LIMIT) {
            output[output_position++] = (uint8_t)symbol;
        } else {
            uint32_t distance_symbol;
            size_t length = (size_t)(symbol - XX_ASH0_LITERAL_LIMIT) +
                            XX_ASH0_MIN_MATCH;
            size_t distance;
            size_t index;
            if (!xx_ash0_read_code(&distance_reader, &distance_tree,
                                   &distance_symbol)) {
                goto cleanup;
            }
            distance = (size_t)distance_symbol + 1U;
            if (length > output_size - output_position ||
                distance > output_position) {
                goto cleanup;
            }
            for (index = 0U; index < length; ++index) {
                output[output_position] = output[output_position - distance];
                ++output_position;
            }
        }
    }
    *tight = ((input_size - (size_t)header->distance_offset) -
              ((distance_reader.bit_position + 7U) >> 3U)) < 4U;
    result = true;
cleanup:
    xx_ash0_tree_cleanup(&distance_tree);
    xx_ash0_tree_cleanup(&symbol_tree);
    return result;
}

bool xx_ash0_decompress_memory(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               unsigned *distance_bits) {
    xx_ash0_header header;
    bool first_tight = false;
    bool second_tight = false;
    bool first_ok;
    bool second_ok;
    if (distance_bits) *distance_bits = 0U;
    if (!input || !output || !xx_ash0_parse_header(input, input_size, &header) ||
        output_size != (size_t)header.uncompressed_size) {
        return false;
    }
    /* A 15-bit distance tree is tried first: a too-narrow tree can otherwise
     * survive briefly with plausible, but wrong, match distances. */
    first_ok = xx_ash0_decode_attempt(input, input_size, &header, output,
                                      output_size, 15U, &first_tight);
    if (first_ok && first_tight) {
        if (distance_bits) *distance_bits = 15U;
        return true;
    }
    second_ok = xx_ash0_decode_attempt(input, input_size, &header, output,
                                       output_size, 11U, &second_tight);
    if (second_ok && second_tight) {
        if (distance_bits) *distance_bits = 11U;
        return true;
    }
    if (first_ok) {
        /* If both loose attempts survived, prefer the wider candidate and
         * materialize it again because the second attempt occupied output. */
        if (second_ok && !xx_ash0_decode_attempt(input, input_size, &header,
                                                  output, output_size, 15U,
                                                  &first_tight)) {
            return false;
        }
        if (distance_bits) *distance_bits = 15U;
        return true;
    }
    if (second_ok) {
        if (distance_bits) *distance_bits = 11U;
        return true;
    }
    return false;
}
