/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GAS stores its static Huffman tree as an MSB-first bit stream.  Multi-bit
 * fields themselves are assembled low-bit first, including the leaf byte and
 * the two encoded child indexes of every internal node.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/gashuff/xx_gashuff.h"

#include <string.h>

#define XX_GASHUFF_HEADER_SIZE 8U
#define XX_GASHUFF_MAX_NODES 511U
#define XX_GASHUFF_MAX_RECORDS (XX_GASHUFF_MAX_NODES + 1U)
#define XX_GASHUFF_MAX_INPUT ((size_t)128U * 1024U * 1024U)
#define XX_GASHUFF_MAX_OUTPUT UINT32_C(0x10000000)

typedef struct xx_gashuff_bit_reader_s {
    const uint8_t *data;
    size_t bit_size;
    size_t bit_position;
} xx_gashuff_bit_reader;

typedef struct xx_gashuff_node_s {
    uint16_t child_zero;
    uint16_t child_one;
    uint8_t symbol;
    bool is_leaf;
} xx_gashuff_node;

static uint16_t xx_gashuff_read16le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_gashuff_read32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool xx_gashuff_read_bit(xx_gashuff_bit_reader *reader,
                                uint32_t *value) {
    size_t bit;
    if (!reader || !value || reader->bit_position >= reader->bit_size) {
        return false;
    }
    bit = reader->bit_position++;
    *value = (uint32_t)((reader->data[bit >> 3U] >>
                         (7U - (unsigned)(bit & 7U))) & 1U);
    return true;
}

static bool xx_gashuff_read_lsb_field(xx_gashuff_bit_reader *reader,
                                      unsigned width, uint32_t *value) {
    uint32_t result = 0U;
    unsigned index;
    if (!reader || !value || width == 0U || width > 16U) return false;
    for (index = 0U; index < width; ++index) {
        uint32_t bit;
        if (!xx_gashuff_read_bit(reader, &bit)) return false;
        result |= bit << index;
    }
    *value = result;
    return true;
}

static bool xx_gashuff_validate_tree(const xx_gashuff_node *nodes,
                                     uint16_t node_count,
                                     uint16_t root_index) {
    uint8_t visited[XX_GASHUFF_MAX_RECORDS];
    uint8_t symbols[256];
    uint16_t stack[XX_GASHUFF_MAX_RECORDS];
    size_t stack_size = 0U;
    size_t visited_count = 0U;
    unsigned internal_count = 0U;
    unsigned leaf_count = 0U;
    uint16_t record_count = (uint16_t)(node_count + 1U);
    if (!nodes || node_count < 3U || root_index >= record_count ||
        nodes[root_index].is_leaf) {
        return false;
    }
    xx_rt_memset(visited, 0, sizeof(visited));
    xx_rt_memset(symbols, 0, sizeof(symbols));
    stack[stack_size++] = root_index;
    while (stack_size != 0U) {
        uint16_t index = stack[--stack_size];
        const xx_gashuff_node *node;
        if (index >= record_count || visited[index]) return false;
        visited[index] = 1U;
        ++visited_count;
        node = &nodes[index];
        if (node->is_leaf) {
            if (symbols[node->symbol]) return false;
            symbols[node->symbol] = 1U;
            ++leaf_count;
        } else {
            if (node->child_zero >= record_count ||
                node->child_one >= record_count ||
                node->child_zero == node->child_one ||
                stack_size > (size_t)record_count - 2U) {
                return false;
            }
            stack[stack_size++] = node->child_zero;
            stack[stack_size++] = node->child_one;
            ++internal_count;
        }
    }
    return leaf_count >= 2U && leaf_count == internal_count + 1U &&
           internal_count == (unsigned)(node_count / 2U) &&
           visited_count == (size_t)record_count - (node_count & 1U);
}

static bool xx_gashuff_parse_internal(const uint8_t *input,
                                      size_t input_size,
                                      xx_gashuff_node *nodes,
                                      xx_gashuff_info *info) {
    xx_gashuff_bit_reader reader;
    uint32_t uncompressed_size;
    uint16_t node_count;
    uint16_t root_index;
    uint16_t record_count;
    uint16_t index;
    unsigned internal_count = 0U;
    size_t max_tree_bytes;
    if (info) xx_rt_memset(info, 0, sizeof(*info));
    if (!input || !nodes || input_size < 11U ||
        input_size > XX_GASHUFF_MAX_INPUT || input_size > SIZE_MAX / 8U) {
        return false;
    }
    uncompressed_size = xx_gashuff_read32le(input);
    node_count = xx_gashuff_read16le(input + 4U);
    root_index = xx_gashuff_read16le(input + 6U);
    if (uncompressed_size == 0U || uncompressed_size > XX_GASHUFF_MAX_OUTPUT ||
        node_count < 3U || node_count > XX_GASHUFF_MAX_NODES) {
        return false;
    }
    record_count = (uint16_t)(node_count + 1U);
    if (root_index >= record_count ||
        (size_t)record_count > (SIZE_MAX - 7U) / 21U) {
        return false;
    }
    max_tree_bytes = ((size_t)record_count * 21U + 7U) / 8U;
    if (input_size - XX_GASHUFF_HEADER_SIZE >
        (size_t)uncompressed_size + max_tree_bytes + 16U) {
        return false;
    }
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.data = input;
    reader.bit_size = input_size * 8U;
    reader.bit_position = XX_GASHUFF_HEADER_SIZE * 8U;
    xx_rt_memset(nodes, 0, XX_GASHUFF_MAX_RECORDS * sizeof(*nodes));
    for (index = 0U; index < record_count; ++index) {
        uint32_t kind;
        if (!xx_gashuff_read_bit(&reader, &kind)) return false;
        if (kind != 0U) {
            uint32_t symbol;
            if (!xx_gashuff_read_lsb_field(&reader, 8U, &symbol)) return false;
            nodes[index].is_leaf = true;
            nodes[index].symbol = (uint8_t)symbol;
        } else {
            uint32_t encoded_one;
            uint32_t encoded_zero;
            if (!xx_gashuff_read_lsb_field(&reader, 10U, &encoded_one) ||
                !xx_gashuff_read_lsb_field(&reader, 10U, &encoded_zero) ||
                (encoded_one & 1U) == 0U || (encoded_zero & 1U) == 0U) {
                return false;
            }
            nodes[index].child_one = (uint16_t)((encoded_one - 1U) >> 1U);
            nodes[index].child_zero = (uint16_t)((encoded_zero - 1U) >> 1U);
            if (nodes[index].child_one >= record_count ||
                nodes[index].child_zero >= record_count) {
                return false;
            }
            ++internal_count;
        }
    }
    if (internal_count != (unsigned)(node_count / 2U) ||
        !xx_gashuff_validate_tree(nodes, node_count, root_index) ||
        reader.bit_size - reader.bit_position < (size_t)uncompressed_size) {
        return false;
    }
    if (info) {
        info->uncompressed_size = uncompressed_size;
        info->node_count = node_count;
        info->root_index = root_index;
        info->payload_bit_offset = reader.bit_position;
        info->table_end_offset = (reader.bit_position + 7U) / 8U;
    }
    return true;
}

bool xx_gashuff_parse_memory(const uint8_t *input, size_t input_size,
                             xx_gashuff_info *info) {
    xx_gashuff_node nodes[XX_GASHUFF_MAX_RECORDS];
    return xx_gashuff_parse_internal(input, input_size, nodes, info);
}

bool xx_gashuff_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size, xx_gashuff_info *info) {
    xx_gashuff_node nodes[XX_GASHUFF_MAX_RECORDS];
    xx_gashuff_info parsed;
    xx_gashuff_bit_reader reader;
    size_t output_position;
    if (consumed_size) *consumed_size = 0U;
    if (info) xx_rt_memset(info, 0, sizeof(*info));
    if (!input || !output ||
        !xx_gashuff_parse_internal(input, input_size, nodes, &parsed) ||
        output_size != (size_t)parsed.uncompressed_size) {
        return false;
    }
    reader.data = input;
    reader.bit_size = input_size * 8U;
    reader.bit_position = parsed.payload_bit_offset;
    for (output_position = 0U; output_position < output_size;
         ++output_position) {
        uint16_t current = parsed.root_index;
        unsigned guard = 0U;
        while (!nodes[current].is_leaf) {
            uint32_t bit;
            if (current >= (uint16_t)(parsed.node_count + 1U) ||
                ++guard > (unsigned)parsed.node_count + 1U ||
                !xx_gashuff_read_bit(&reader, &bit)) {
                return false;
            }
            current = bit != 0U ? nodes[current].child_one
                                : nodes[current].child_zero;
        }
        output[output_position] = nodes[current].symbol;
    }
    if (consumed_size) *consumed_size = input_size;
    if (info) *info = parsed;
    return true;
}
