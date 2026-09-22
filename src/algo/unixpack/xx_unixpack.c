/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/unixpack/xx_unixpack.h"

#include <string.h>

#define XX_PACK_MAX_LEVEL 24U
#define XX_PACK_MAX_SYMBOLS 257U
#define XX_PACK_MAX_NODES 514U

typedef struct xx_pack_node_s {
    int16_t child[2];
    int16_t symbol;
} xx_pack_node;

static bool xx_pack_insert(xx_pack_node *nodes, size_t *node_count,
                           uint32_t code, unsigned length, int symbol) {
    size_t node = 0U;
    unsigned i;
    for (i = 0U; i < length; ++i) {
        unsigned bit = (code >> (length - i - 1U)) & 1U;
        int16_t next;
        if (nodes[node].symbol >= 0) return false;
        next = nodes[node].child[bit];
        if (next < 0) {
            if (*node_count >= XX_PACK_MAX_NODES) return false;
            next = (int16_t)(*node_count);
            nodes[node].child[bit] = next;
            nodes[*node_count].child[0] = -1;
            nodes[*node_count].child[1] = -1;
            nodes[*node_count].symbol = -1;
            ++*node_count;
        }
        node = (size_t)next;
    }
    if (nodes[node].symbol >= 0 || nodes[node].child[0] >= 0 ||
        nodes[node].child[1] >= 0) return false;
    nodes[node].symbol = (int16_t)symbol;
    return true;
}

static bool xx_pack_decode_modern(const uint8_t *input, size_t input_size,
                                  uint8_t *output, size_t output_size,
                                  size_t *written, bool require_end_code) {
    uint8_t counts[XX_PACK_MAX_LEVEL];
    xx_pack_node nodes[XX_PACK_MAX_NODES];
    size_t input_pos = 0U;
    size_t output_pos = 0U;
    size_t node_count = 1U;
    size_t bit_pos;
    uint32_t code = UINT32_C(0x01000000);
    unsigned max_level;
    unsigned level;

    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        input_size < 2U) return false;
    max_level = input[input_pos++];
    if (max_level == 0U || max_level > XX_PACK_MAX_LEVEL ||
        input_size - input_pos < max_level) return false;
    xx_rt_memset(counts, 0, sizeof(counts));
    xx_rt_memcpy(counts, input + input_pos, max_level);
    input_pos += max_level;
    if (counts[max_level - 1U] > 253U) return false;
    counts[max_level - 1U] = (uint8_t)(counts[max_level - 1U] + 2U);

    nodes[0].child[0] = -1;
    nodes[0].child[1] = -1;
    nodes[0].symbol = -1;
    for (level = 0U; level < max_level; ++level) {
        uint32_t step = UINT32_C(1) << (23U - level);
        unsigned j;
        if ((uint64_t)counts[level] * step > code) return false;
        code -= (uint32_t)counts[level] * step;
        for (j = 0U; j < counts[level]; ++j) {
            int symbol;
            uint32_t bits = code >> (23U - level);
            if (level == max_level - 1U && j == counts[level] - 1U) {
                symbol = 256;
            } else {
                if (input_pos >= input_size) return false;
                symbol = input[input_pos++];
            }
            if (!xx_pack_insert(nodes, &node_count, bits, level + 1U,
                                symbol)) return false;
            code += step;
        }
        code -= (uint32_t)counts[level] * step;
    }

    bit_pos = input_pos * 8U;
    while (output_pos < output_size) {
        size_t node = 0U;
        while (nodes[node].symbol < 0) {
            unsigned bit;
            int16_t next;
            if (bit_pos >= input_size * 8U) return false;
            bit = (input[bit_pos >> 3U] >> (7U - (bit_pos & 7U))) & 1U;
            ++bit_pos;
            next = nodes[node].child[bit];
            if (next < 0) return false;
            node = (size_t)next;
        }
        if (nodes[node].symbol == 256) return false;
        output[output_pos++] = (uint8_t)nodes[node].symbol;
    }
    if (require_end_code) {
        size_t node = 0U;
        while (nodes[node].symbol < 0) {
            unsigned bit;
            int16_t next;
            if (bit_pos >= input_size * 8U) return false;
            bit = (input[bit_pos >> 3U] >> (7U - (bit_pos & 7U))) & 1U;
            ++bit_pos;
            next = nodes[node].child[bit];
            if (next < 0) return false;
            node = (size_t)next;
        }
        if (nodes[node].symbol != 256) return false;
    }
    if (written) *written = output_pos;
    return true;
}

bool xx_unixpack_decode_raw(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size,
                            size_t *written) {
    return xx_pack_decode_modern(input, input_size, output, output_size,
                                 written, false);
}

static uint16_t xx_pack_read16le(const uint8_t *input) {
    return (uint16_t)input[0] | (uint16_t)((uint16_t)input[1] << 8U);
}

static uint32_t xx_pack_read32be(const uint8_t *input) {
    return ((uint32_t)input[0] << 24U) | ((uint32_t)input[1] << 16U) |
           ((uint32_t)input[2] << 8U) | (uint32_t)input[3];
}

static bool xx_pack_build_old_tree(const uint16_t *tree, size_t tree_count,
                                   xx_pack_node *nodes, size_t *node_count,
                                   size_t node, unsigned length,
                                   uint32_t bits) {
    uint32_t left;
    uint32_t right;
    if (!tree || !nodes || !node_count || node >= tree_count ||
        node + 1U >= tree_count) {
        return false;
    }
    if (tree[node] == 0U) {
        return length != 0U &&
               xx_pack_insert(nodes, node_count, bits, length,
                              (int)tree[node + 1U]);
    }
    if (length >= XX_PACK_MAX_LEVEL || tree[node] > UINT32_MAX - node ||
        tree[node + 1U] > UINT32_MAX - node) {
        return false;
    }
    left = (uint32_t)node + tree[node];
    right = (uint32_t)node + tree[node + 1U];
    return xx_pack_build_old_tree(tree, tree_count, nodes, node_count, left,
                                  length + 1U, bits << 1U) &&
           xx_pack_build_old_tree(tree, tree_count, nodes, node_count, right,
                                  length + 1U, (bits << 1U) | 1U);
}

static bool xx_pack_read_old_bit(const uint8_t *input, size_t input_size,
                                 size_t *input_pos, uint16_t *word,
                                 unsigned *bits_left, unsigned *bit) {
    if (!input || !input_pos || !word || !bits_left || !bit) return false;
    if (*bits_left == 0U) {
        if (*input_pos > input_size || input_size - *input_pos < 2U) {
            return false;
        }
        *word = xx_pack_read16le(input + *input_pos);
        *input_pos += 2U;
        *bits_left = 16U;
    }
    *bit = (unsigned)((*word >> (*bits_left - 1U)) & 1U);
    --*bits_left;
    return true;
}

static bool xx_pack_decode_old(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               size_t *written) {
    uint16_t tree[1024];
    xx_pack_node nodes[XX_PACK_MAX_NODES];
    size_t tree_count;
    size_t input_pos = 0U;
    size_t node_count = 1U;
    size_t output_pos = 0U;
    uint16_t word = 0U;
    unsigned bits_left = 0U;
    size_t index;
    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        input_size < 2U) {
        return false;
    }
    tree_count = xx_pack_read16le(input);
    input_pos = 2U;
    if (tree_count < 2U || tree_count >= 1024U) return false;
    for (index = 0U; index < tree_count; ++index) {
        uint8_t value;
        if (input_pos >= input_size) return false;
        value = input[input_pos++];
        if (value == UINT8_C(0xff)) {
            if (input_size - input_pos < 2U) return false;
            tree[index] = xx_pack_read16le(input + input_pos);
            input_pos += 2U;
        } else {
            tree[index] = value;
        }
    }
    nodes[0].child[0] = -1;
    nodes[0].child[1] = -1;
    nodes[0].symbol = -1;
    if (!xx_pack_build_old_tree(tree, tree_count, nodes, &node_count, 0U,
                                0U, 0U)) {
        return false;
    }
    while (output_pos < output_size) {
        size_t node = 0U;
        while (nodes[node].symbol < 0) {
            unsigned bit;
            int16_t next;
            if (!xx_pack_read_old_bit(input, input_size, &input_pos, &word,
                                      &bits_left, &bit)) {
                return false;
            }
            next = nodes[node].child[bit];
            if (next < 0) return false;
            node = (size_t)next;
        }
        output[output_pos++] = (uint8_t)nodes[node].symbol;
    }
    if (written) *written = output_pos;
    return true;
}

bool xx_unixpack_parse_header(const void *source, size_t source_size,
                              uint64_t *uncompressed_size,
                              bool *is_old_version) {
    const uint8_t *input = (const uint8_t *)source;
    bool old_version;
    uint64_t raw_size;
    if (uncompressed_size) *uncompressed_size = 0U;
    if (is_old_version) *is_old_version = false;
    if (!input || source_size < 6U || input[0] != UINT8_C(0x1f) ||
        (input[1] != UINT8_C(0x1e) && input[1] != UINT8_C(0x1f))) {
        return false;
    }
    old_version = input[1] == UINT8_C(0x1f);
    raw_size = old_version ?
                   ((uint64_t)xx_pack_read16le(input + 2U) << 16U) |
                       xx_pack_read16le(input + 4U) :
                   xx_pack_read32be(input + 2U);
    if (old_version && raw_size == 0U) return false;
    if (uncompressed_size) *uncompressed_size = raw_size;
    if (is_old_version) *is_old_version = old_version;
    return true;
}

bool xx_unixpack_decode_memory(const void *source, size_t source_size,
                               void *destination, size_t destination_size,
                               size_t *written) {
    const uint8_t *input = (const uint8_t *)source;
    uint8_t *output = (uint8_t *)destination;
    uint64_t raw_size;
    bool old_version;
    if (written) *written = 0U;
    if ((!output && destination_size != 0U) ||
        !xx_unixpack_parse_header(input, source_size, &raw_size,
                                  &old_version) ||
        raw_size > (uint64_t)SIZE_MAX || destination_size != (size_t)raw_size) {
        return false;
    }
    if (old_version) {
        return xx_pack_decode_old(input + 6U, source_size - 6U, output,
                                  destination_size, written);
    }
    return xx_pack_decode_modern(input + 6U, source_size - 6U, output,
                                 destination_size, written, true);
}
