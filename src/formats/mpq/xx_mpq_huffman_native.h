/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Bounded C adaptation of StormLib's adaptive MPQ Huffman reader. The
 * distribution tables and sibling-list update algorithm originate from
 * Ladislav Zezula/ShadowFlare, StormLib huff.cpp commit 44ebfbf. The complete
 * original MIT grant and attribution are retained in the adjacent table
 * header. No StormLib code is linked into xxfclib.
 */
#ifndef XX_MPQ_HUFFMAN_NATIVE_H
#define XX_MPQ_HUFFMAN_NATIVE_H

#include "xx_mpq_huffman_tables.h"
#include "xxfclib/data/xx_pd.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MPQ_HUFF_NODE_COUNT 515U
#define MPQ_HUFF_VALUE_COUNT 258U
#define MPQ_HUFF_ERROR 0x1ffU

typedef struct mpq_huff_node_s mpq_huff_node;
struct mpq_huff_node_s {
    mpq_huff_node *next, *prev, *parent, *lo;
    unsigned value, weight;
};
typedef struct mpq_huff_tree_s {
    mpq_huff_node nodes[MPQ_HUFF_NODE_COUNT], head;
    mpq_huff_node *by_value[MPQ_HUFF_VALUE_COUNT];
    size_t used;
} mpq_huff_tree;
typedef struct mpq_huff_bits_s {
    const uint8_t *input;
    size_t size, at;
    unsigned bits, count;
} mpq_huff_bits;

static bool mpq_huff_bit(mpq_huff_bits *b, unsigned *bit) {
    if (!b || !bit) return false;
    if (b->count == 0U) {
        if (b->at >= b->size) return false;
        b->bits = b->input[b->at++];
        b->count = 8U;
    }
    *bit = b->bits & 1U;
    b->bits >>= 1U;
    --b->count;
    return true;
}
static bool mpq_huff_byte(mpq_huff_bits *b, unsigned *value) {
    unsigned n, bit;
    if (!value) return false;
    *value = 0U;
    for (n = 0U; n < 8U; ++n) {
        if (!mpq_huff_bit(b, &bit)) return false;
        *value |= bit << n;
    }
    return true;
}
static void mpq_huff_unlink(mpq_huff_node *node) {
    if (node->next) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        node->next = node->prev = NULL;
    }
}
static void mpq_huff_link_after(mpq_huff_node *point,
                                mpq_huff_node *node) {
    node->next = point->next;
    node->prev = point;
    point->next->prev = node;
    point->next = node;
}
static mpq_huff_node *mpq_huff_new(mpq_huff_tree *tree, unsigned value,
                                   unsigned weight, bool before_head) {
    mpq_huff_node *node;
    if (tree->used >= MPQ_HUFF_NODE_COUNT) return NULL;
    node = &tree->nodes[tree->used++];
    memset(node, 0, sizeof(*node));
    mpq_huff_link_after(before_head ? tree->head.prev : &tree->head, node);
    node->value = value;
    node->weight = weight;
    return node;
}
static mpq_huff_node *mpq_huff_higher(mpq_huff_tree *tree,
                                      mpq_huff_node *at, unsigned weight) {
    size_t steps = 0U;
    while (at != &tree->head && steps++ <= MPQ_HUFF_NODE_COUNT) {
        if (!at) return NULL;
        if (at->weight >= weight) return at;
        at = at->prev;
    }
    return steps <= MPQ_HUFF_NODE_COUNT + 1U ? &tree->head : NULL;
}
static bool mpq_huff_fixup(mpq_huff_tree *tree, mpq_huff_node *node,
                           unsigned *max_weight) {
    if (node->weight < *max_weight) {
        mpq_huff_node *higher = mpq_huff_higher(
            tree, tree->head.prev, node->weight);
        if (!higher) return false;
        mpq_huff_unlink(node);
        mpq_huff_link_after(higher, node);
    } else {
        *max_weight = node->weight;
    }
    return true;
}
static bool mpq_huff_build(mpq_huff_tree *tree, unsigned type) {
    const uint8_t *dist;
    mpq_huff_node *lo, *hi, *parent;
    unsigned value, max_weight = 0U;
    size_t steps = 0U;
    if (!tree || (type & 15U) >= 9U) return false;
    memset(tree, 0, sizeof(*tree));
    tree->head.next = tree->head.prev = &tree->head;
    dist = mpq_huff_distributions[type & 15U];
    for (value = 0U; value < 256U; ++value) {
        if (dist[value] != 0U) {
            tree->by_value[value] = mpq_huff_new(tree, value, dist[value], false);
            if (!tree->by_value[value] ||
                !mpq_huff_fixup(tree, tree->by_value[value], &max_weight))
                return false;
        }
    }
    tree->by_value[256U] = mpq_huff_new(tree, 256U, 1U, true);
    tree->by_value[257U] = mpq_huff_new(tree, 257U, 1U, true);
    if (!tree->by_value[256U] || !tree->by_value[257U]) return false;
    lo = tree->head.prev;
    while (lo != &tree->head && ++steps <= MPQ_HUFF_NODE_COUNT) {
        hi = lo->prev;
        if (hi == &tree->head) break;
        if (hi->weight > UINT32_MAX - lo->weight) return false;
        parent = mpq_huff_new(tree, 0U, hi->weight + lo->weight, false);
        if (!parent) return false;
        lo->parent = hi->parent = parent;
        parent->lo = lo;
        if (!mpq_huff_fixup(tree, parent, &max_weight)) return false;
        lo = hi->prev;
    }
    return steps <= MPQ_HUFF_NODE_COUNT;
}
static bool mpq_huff_increment(mpq_huff_tree *tree, mpq_huff_node *node) {
    size_t steps = 0U;
    while (node && ++steps <= MPQ_HUFF_NODE_COUNT) {
        mpq_huff_node *higher, *hi, *old_parent, *other_parent, *other_lo;
        if (node->weight == UINT32_MAX) return false;
        ++node->weight;
        higher = mpq_huff_higher(tree, node->prev, node->weight);
        if (!higher) return false;
        hi = higher->next;
        if (hi != node) {
            if (hi == &tree->head || !hi->parent || !node->parent ||
                !hi->parent->lo) return false;
            other_parent = hi->parent;
            old_parent = node->parent;
            other_lo = other_parent->lo;
            mpq_huff_unlink(hi);
            mpq_huff_link_after(node, hi);
            mpq_huff_unlink(node);
            mpq_huff_link_after(higher, node);
            if (old_parent->lo == node) old_parent->lo = hi;
            if (other_lo == hi) other_parent->lo = node;
            node->parent = other_parent;
            hi->parent = old_parent;
        }
        node = node->parent;
    }
    return node == NULL;
}
static bool mpq_huff_insert(mpq_huff_tree *tree,
                            unsigned old_value, unsigned new_value) {
    mpq_huff_node *last, *hi, *lo;
    if (old_value >= MPQ_HUFF_VALUE_COUNT || new_value >= 256U ||
        tree->by_value[new_value] || tree->used > MPQ_HUFF_NODE_COUNT - 2U)
        return false;
    last = tree->head.prev;
    if (last == &tree->head) return false;
    hi = mpq_huff_new(tree, old_value, last->weight, true);
    lo = mpq_huff_new(tree, new_value, 0U, true);
    if (!hi || !lo) return false;
    hi->parent = lo->parent = last;
    last->lo = lo;
    tree->by_value[old_value] = hi;
    tree->by_value[new_value] = lo;
    return mpq_huff_increment(tree, lo);
}
static bool mpq_huff_symbol(mpq_huff_tree *tree, mpq_huff_bits *bits,
                            unsigned *value) {
    mpq_huff_node *node = tree->head.next;
    size_t depth = 0U;
    if (!value || node == &tree->head) return false;
    while (node->lo && ++depth <= MPQ_HUFF_NODE_COUNT) {
        unsigned bit;
        mpq_huff_node *lo = node->lo;
        mpq_huff_node *hi = lo->prev;
        if (!mpq_huff_bit(bits, &bit) || !lo || !hi ||
            lo->parent != node || hi == &tree->head || hi->parent != node)
            return false;
        node = bit ? hi : lo;
    }
    if (depth > MPQ_HUFF_NODE_COUNT || node->value >= MPQ_HUFF_VALUE_COUNT)
        return false;
    *value = node->value;
    return true;
}
static bool mpq_huff_decode(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_capacity,
                            size_t *output_size, xx_pd_struct *pd) {
    mpq_huff_tree tree;
    mpq_huff_bits bits;
    unsigned type, symbol;
    size_t written = 0U;
    if (output_size) *output_size = 0U;
    if (!input || !output || input_size < 2U || output_capacity == 0U ||
        (pd && xx_pd_is_stopped(pd))) return false;
    memset(&bits, 0, sizeof(bits));
    bits.input = input;
    bits.size = input_size;
    if (!mpq_huff_byte(&bits, &type) || !mpq_huff_build(&tree, type))
        return false;
    for (;;) {
        if ((written & 255U) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        if (!mpq_huff_symbol(&tree, &bits, &symbol)) return false;
        if (symbol == 256U) break;
        if (symbol == 257U) {
            unsigned old_value = tree.head.prev->value;
            if (!mpq_huff_byte(&bits, &symbol) ||
                !mpq_huff_insert(&tree, old_value, symbol)) return false;
            if (type != 0U &&
                !mpq_huff_increment(&tree, tree.by_value[symbol])) return false;
        }
        if (symbol >= 256U || written >= output_capacity) return false;
        output[written++] = (uint8_t)symbol;
        if (type == 0U &&
            !mpq_huff_increment(&tree, tree.by_value[symbol])) return false;
    }
    if (bits.at != input_size || bits.bits != 0U ||
        (pd && xx_pd_is_stopped(pd))) return false;
    if (output_size) *output_size = written;
    return true;
}

#endif /* XX_MPQ_HUFFMAN_NATIVE_H */
