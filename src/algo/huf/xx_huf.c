/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HUF archives encode one static binary tree in pre-order. Tree and member
 * streams are both packed least-significant-bit first, but a set data bit
 * selects child zero and a clear data bit selects child one.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/huf/xx_huf.h"

#include <string.h>

typedef struct xx_huf_bits_s {
    const uint8_t *data;
    size_t size;
    size_t byte_position;
    uint8_t byte_value;
    unsigned bits_used;
} xx_huf_bits;

typedef struct xx_huf_pending_s {
    int16_t parent_index;
    uint8_t child_slot;
} xx_huf_pending;

static bool xx_huf_read_bit(xx_huf_bits *bits, uint32_t *value) {
    if (!bits || !value) return false;
    if (bits->bits_used == 8U) {
        if (!bits->data || bits->byte_position >= bits->size) return false;
        bits->byte_value = bits->data[bits->byte_position++];
        bits->bits_used = 0U;
    }
    *value = (uint32_t)((bits->byte_value >> bits->bits_used) & 1U);
    ++bits->bits_used;
    return true;
}

static bool xx_huf_tree_is_usable(const xx_huf_tree *tree) {
    uint16_t index;
    if (!tree || tree->node_count == 0U ||
        tree->node_count > XX_HUF_MAX_NODES) {
        return false;
    }
    for (index = 0U; index < tree->node_count; ++index) {
        const xx_huf_node *node = &tree->nodes[index];
        if (node->is_leaf) continue;
        if (node->child_zero < 0 || node->child_one < 0 ||
            node->child_zero >= (int16_t)tree->node_count ||
            node->child_one >= (int16_t)tree->node_count ||
            node->child_zero == node->child_one) {
            return false;
        }
    }
    return true;
}

static bool xx_huf_decode_symbol(const xx_huf_tree *tree, xx_huf_bits *bits,
                                 uint8_t *symbol) {
    int16_t current = 0;
    uint16_t guard = 0U;
    if (!tree || !bits || !symbol) return false;
    while (!tree->nodes[current].is_leaf) {
        uint32_t bit;
        if (++guard > tree->node_count || !xx_huf_read_bit(bits, &bit)) {
            return false;
        }
        current = bit != 0U ? tree->nodes[current].child_zero
                            : tree->nodes[current].child_one;
    }
    *symbol = tree->nodes[current].symbol;
    return true;
}

bool xx_huf_build_tree(const uint8_t *tree_data, size_t tree_size,
                       const uint8_t *symbols, size_t symbol_count,
                       xx_huf_tree *tree, size_t *tree_bytes_used) {
    xx_huf_bits bits;
    xx_huf_pending pending[XX_HUF_MAX_NODES];
    uint8_t seen_symbols[256];
    size_t pending_count = 0U;
    size_t symbol_index = 0U;
    size_t index;

    if (tree_bytes_used) *tree_bytes_used = 0U;
    if (!tree || !tree_data || !symbols || tree_size == 0U ||
        symbol_count == 0U || symbol_count > XX_HUF_MAX_SYMBOLS) {
        return false;
    }
    xx_rt_memset(tree, 0, sizeof(*tree));
    xx_rt_memset(seen_symbols, 0, sizeof(seen_symbols));
    for (index = 0U; index < symbol_count; ++index) {
        if (seen_symbols[symbols[index]]) return false;
        seen_symbols[symbols[index]] = 1U;
    }
    xx_rt_memset(&bits, 0, sizeof(bits));
    bits.data = tree_data;
    bits.size = tree_size;
    bits.bits_used = 8U;
    pending[pending_count].parent_index = -1;
    pending[pending_count].child_slot = 0U;
    ++pending_count;

    while (pending_count != 0U) {
        xx_huf_pending item;
        xx_huf_node *node;
        uint32_t kind;
        uint16_t node_index;
        if (tree->node_count >= XX_HUF_MAX_NODES) return false;
        item = pending[--pending_count];
        node_index = tree->node_count++;
        node = &tree->nodes[node_index];
        node->child_zero = -1;
        node->child_one = -1;
        node->symbol = 0U;
        node->is_leaf = false;
        if (item.parent_index >= 0) {
            xx_huf_node *parent;
            if ((uint16_t)item.parent_index >= node_index) return false;
            parent = &tree->nodes[item.parent_index];
            if (parent->is_leaf) return false;
            if (item.child_slot == 0U) {
                if (parent->child_zero >= 0) return false;
                parent->child_zero = (int16_t)node_index;
            } else {
                if (parent->child_one >= 0) return false;
                parent->child_one = (int16_t)node_index;
            }
        }
        if (!xx_huf_read_bit(&bits, &kind)) return false;
        if (kind == 0U) {
            if (symbol_index >= symbol_count) return false;
            node->is_leaf = true;
            node->symbol = symbols[symbol_index++];
        } else {
            if (pending_count > XX_HUF_MAX_NODES - 2U) return false;
            pending[pending_count].parent_index = (int16_t)node_index;
            pending[pending_count].child_slot = 1U;
            ++pending_count;
            pending[pending_count].parent_index = (int16_t)node_index;
            pending[pending_count].child_slot = 0U;
            ++pending_count;
        }
    }
    if (symbol_index != symbol_count ||
        tree->node_count != (uint16_t)(symbol_count * 2U - 1U) ||
        !xx_huf_tree_is_usable(tree)) {
        return false;
    }
    if (tree_bytes_used) *tree_bytes_used = bits.byte_position;
    return true;
}

bool xx_huf_decode_memory(const xx_huf_tree *tree, const uint8_t *input,
                          size_t input_size, uint8_t *output,
                          size_t output_size, size_t *input_bytes_used) {
    xx_huf_bits bits;
    size_t index;
    if (input_bytes_used) *input_bytes_used = 0U;
    if (!tree || (!input && input_size != 0U) ||
        (!output && output_size != 0U) || !xx_huf_tree_is_usable(tree)) {
        return false;
    }
    if (output_size != 0U && (!input || input_size == 0U)) return false;
    xx_rt_memset(&bits, 0, sizeof(bits));
    bits.data = input;
    bits.size = input_size;
    bits.bits_used = 8U;
    for (index = 0U; index < output_size; ++index) {
        if (!xx_huf_decode_symbol(tree, &bits, output + index)) return false;
    }
    if (input_bytes_used) *input_bytes_used = bits.byte_position;
    return true;
}

bool xx_huf_decode_cstring(const xx_huf_tree *tree, const uint8_t *input,
                           size_t input_size, char *output,
                           size_t output_capacity, size_t max_characters,
                           size_t *output_length, size_t *input_bytes_used) {
    xx_huf_bits bits;
    size_t length = 0U;
    if (output_length) *output_length = 0U;
    if (input_bytes_used) *input_bytes_used = 0U;
    if (!tree || !input || input_size == 0U || !output ||
        output_capacity == 0U || max_characters >= output_capacity ||
        !xx_huf_tree_is_usable(tree)) {
        return false;
    }
    output[0] = '\0';
    xx_rt_memset(&bits, 0, sizeof(bits));
    bits.data = input;
    bits.size = input_size;
    bits.bits_used = 8U;
    for (;;) {
        uint8_t symbol;
        if (!xx_huf_decode_symbol(tree, &bits, &symbol)) return false;
        if (symbol == 0U) {
            output[length] = '\0';
            if (output_length) *output_length = length;
            if (input_bytes_used) *input_bytes_used = bits.byte_position;
            return true;
        }
        if (length >= max_characters) return false;
        output[length++] = (char)symbol;
    }
}
