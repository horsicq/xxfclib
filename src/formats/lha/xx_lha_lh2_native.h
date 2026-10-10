/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * Native LHarc -lh2- adaptive-Huffman decoder. The wire algorithm is
 * described by the original LHa 1.14i dhuf.c and Ancient's BSD-2-Clause
 * LH2Decompressor.cpp. This bounded implementation was written afresh.
 *
 * Original LHarc/UNLHA32 encoders do not emit LH2 for files over 8 KiB.
 * Decoding is limited to that range: longer streams need a separately
 * validated adaptive-tree frequency reconstruction implementation.
 */
#ifndef XX_LHA_LH2_NATIVE_H
#define XX_LHA_LH2_NATIVE_H

#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define XX_LHA_LH2_LIMIT 8192U
#define XX_LHA_LH2_C_CODES 286U
#define XX_LHA_LH2_P_CODES 128U

typedef struct xx_lha_lh2_bits_s {
    const uint8_t *data;
    size_t size, position;
} xx_lha_lh2_bits;

typedef struct xx_lha_lh2_node_s {
    unsigned frequency, index, parent, left, right;
} xx_lha_lh2_node;

typedef struct xx_lha_lh2_tree_s {
    xx_lha_lh2_node nodes[XX_LHA_LH2_C_CODES * 2U - 1U];
    unsigned order[XX_LHA_LH2_C_CODES * 2U - 1U];
    unsigned maximum, count;
} xx_lha_lh2_tree;

static bool xx_lha_lh2_bits_get(xx_lha_lh2_bits *bits, unsigned count, unsigned *value)
{
    unsigned result = 0U, i;
    if (!bits || !value || count > 8U || bits->size > SIZE_MAX / 8U || bits->position > bits->size * 8U || count > bits->size * 8U - bits->position) return false;
    for (i = 0U; i < count; ++i) {
        size_t bit = bits->position++;
        result = (result << 1U) | ((bits->data[bit / 8U] >> (7U - (unsigned)(bit & 7U))) & 1U);
    }
    *value = result;
    return true;
}

static bool xx_lha_lh2_tree_init(xx_lha_lh2_tree *tree, unsigned maximum, unsigned count)
{
    unsigned i;
    if (!tree || maximum > XX_LHA_LH2_C_CODES || count > maximum || maximum < 2U) return false;
    memset(tree, 0, sizeof(*tree));
    tree->maximum = maximum;
    tree->count = count;
    for (i = 0U; i < count; ++i) {
        unsigned index = i + (maximum - count) * 2U;
        tree->nodes[i].frequency = 1U;
        tree->nodes[i].index = index;
        tree->nodes[i].parent = maximum * 2U - count + i / 2U;
        tree->order[index] = i;
    }
    for (i = maximum * 2U - count; i < maximum * 2U - 1U; ++i) {
        unsigned j = (i - (maximum * 2U - count)) * 2U;
        unsigned left = j >= count ? j + (maximum - count) * 2U : j;
        unsigned right = j + 1U >= count ? j + 1U + (maximum - count) * 2U : j + 1U;
        xx_lha_lh2_node *node = &tree->nodes[i];
        if (left >= maximum * 2U - 1U || right >= maximum * 2U - 1U) return false;
        node->frequency = tree->nodes[left].frequency + tree->nodes[right].frequency;
        node->index = i;
        node->parent = maximum + i / 2U;
        node->left = left;
        node->right = right;
        tree->order[i] = i;
    }
    return true;
}

static bool xx_lha_lh2_tree_add(xx_lha_lh2_tree *tree)
{
    unsigned maximum, count, new_index, insert_index, insert_node;
    unsigned representative, parent;
    xx_lha_lh2_node *node;
    if (!tree || tree->count >= tree->maximum) return false;
    maximum = tree->maximum;
    count = tree->count;
    new_index = (maximum - count - 1U) * 2U;
    if (!count) {
        tree->nodes[0U].frequency = 0U;
        tree->nodes[0U].index = new_index - 1U;
        tree->nodes[0U].parent = maximum * 2U - 2U;
        tree->order[new_index - 1U] = 0U;
        tree->count = 1U;
        return true;
    }
    node = &tree->nodes[count];
    node->frequency = 0U;
    node->index = new_index;
    node->parent = maximum * 2U - count - 1U;
    tree->order[new_index] = count;
    insert_index = new_index + 2U;
    insert_node = maximum * 2U - count - 1U;
    if (count > 1U) {
        tree->order[insert_index - 1U] = tree->order[insert_index];
        --tree->nodes[tree->order[insert_index - 1U]].index;
        representative = tree->order[(maximum - count) * 2U];
        parent = tree->nodes[representative].parent;
        if (parent >= maximum * 2U - 1U) return false;
        if (tree->nodes[parent].left == representative) tree->nodes[parent].left = insert_node;
        else if (tree->nodes[parent].right == representative) tree->nodes[parent].right = insert_node;
        else return false;
        tree->nodes[representative].parent = insert_node;
    } else {
        representative = 0U;
        parent = maximum * 2U - 1U; /* sentinel, not dereferenced */
    }
    node = &tree->nodes[insert_node];
    node->frequency = tree->nodes[representative].frequency;
    node->index = insert_index;
    node->parent = parent;
    node->left = count;
    node->right = representative;
    tree->order[insert_index] = insert_node;
    if (count > 1U && tree->nodes[tree->nodes[parent].left].index > tree->nodes[tree->nodes[parent].right].index) {
        unsigned temporary = tree->nodes[parent].left;
        tree->nodes[parent].left = tree->nodes[parent].right;
        tree->nodes[parent].right = temporary;
    }
    tree->count = count + 1U;
    return true;
}

static bool xx_lha_lh2_tree_update(xx_lha_lh2_tree *tree, unsigned code)
{
    unsigned root, index, target, frequency, other;
    if (!tree || code >= tree->count) return false;
    if (tree->count == 1U) {
        tree->nodes[0U].frequency = 1U; /* original LH2 single-node rule */
        return true;
    }
    root = tree->maximum * 2U - 2U;
    while (code != root) {
        unsigned a_parent, b_parent, a_left, b_left, temporary;
        xx_lha_lh2_node *node = &tree->nodes[code];
        ++node->frequency;
        index = node->index;
        target = index;
        frequency = node->frequency;
        while (target < root && frequency > tree->nodes[tree->order[target + 1U]].frequency) ++target;
        if (target != index) {
            other = tree->order[target];
            a_parent = node->parent;
            b_parent = tree->nodes[other].parent;
            if (a_parent >= root + 1U || b_parent >= root + 1U) return false;
            a_left = tree->nodes[a_parent].left == code;
            b_left = tree->nodes[b_parent].left == other;
            if ((!a_left && tree->nodes[a_parent].right != code) || (!b_left && tree->nodes[b_parent].right != other)) return false;
            temporary = node->index;
            node->index = tree->nodes[other].index;
            tree->nodes[other].index = temporary;
            tree->order[index] = other;
            tree->order[target] = code;
            if (a_left) tree->nodes[a_parent].left = other;
            else tree->nodes[a_parent].right = other;
            if (b_left) tree->nodes[b_parent].left = code;
            else tree->nodes[b_parent].right = code;
            node->parent = b_parent;
            tree->nodes[other].parent = a_parent;
        }
        code = tree->nodes[code].parent;
        if (code >= root + 1U) return false;
    }
    ++tree->nodes[root].frequency;
    return true;
}

static bool xx_lha_lh2_tree_decode(xx_lha_lh2_tree *tree, xx_lha_lh2_bits *bits, unsigned *symbol)
{
    unsigned code, bit, steps = 0U;
    if (!tree || !bits || !symbol || !tree->count) return false;
    if (tree->count == 1U) {
        *symbol = 0U;
        return true;
    }
    code = tree->maximum * 2U - 2U;
    while (code >= tree->maximum) {
        if (++steps > 32U || !xx_lha_lh2_bits_get(bits, 1U, &bit)) return false;
        code = bit ? tree->nodes[code].right : tree->nodes[code].left;
        if (code >= tree->maximum * 2U - 1U) return false;
    }
    *symbol = code;
    return code < tree->count;
}

static bool xx_lha_lh2_decode_native(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, xx_pd_struct *pd)
{
    xx_lha_lh2_tree literal, position;
    xx_lha_lh2_bits bits;
    size_t produced = 0U;
    if (!input || !output || !output_size || output_size > XX_LHA_LH2_LIMIT || input_size > SIZE_MAX / 8U ||
        !xx_lha_lh2_tree_init(&literal, XX_LHA_LH2_C_CODES, XX_LHA_LH2_C_CODES) || !xx_lha_lh2_tree_init(&position, XX_LHA_LH2_P_CODES, 0U))
        return false;
    bits.data = input;
    bits.size = input_size;
    bits.position = 0U;
    while (produced < output_size) {
        unsigned symbol;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_lha_lh2_tree_decode(&literal, &bits, &symbol) || !xx_lha_lh2_tree_update(&literal, symbol)) return false;
        if (symbol == 285U) {
            unsigned extra;
            if (!xx_lha_lh2_bits_get(&bits, 8U, &extra)) return false;
            symbol += extra;
        }
        if (symbol < 256U) {
            output[produced++] = (uint8_t)symbol;
        } else {
            unsigned group, low, length = symbol - 253U;
            size_t distance, i;
            unsigned groups = (unsigned)((produced + 63U) / 64U);
            if (length < 3U || length > 256U || length > output_size - produced || groups > XX_LHA_LH2_P_CODES) return false;
            while (position.count < groups) {
                unsigned new_group = position.count;
                if (!xx_lha_lh2_tree_add(&position) || !xx_lha_lh2_tree_update(&position, new_group)) return false;
            }
            if (!xx_lha_lh2_tree_decode(&position, &bits, &group) || !xx_lha_lh2_tree_update(&position, group) || !xx_lha_lh2_bits_get(&bits, 6U, &low)) return false;
            distance = ((size_t)group << 6U) + low + 1U;
            if (distance > XX_LHA_LH2_LIMIT) return false;
            for (i = 0U; i < length; ++i) {
                output[produced] = produced < distance ? (uint8_t)' ' : output[produced - distance];
                ++produced;
            }
        }
    }
    return (bits.position + 7U) / 8U == input_size && (!pd || !xx_pd_is_stopped(pd));
}
#endif /* XX_LHA_LH2_NATIVE_H */
