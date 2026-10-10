/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient LHDecompressor.cpp and
 * DynamicHuffmanDecoder.hpp at commit
 * 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_LHLB_NATIVE_H
#define XX_XPK_LHLB_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define XPK_LHLB_SYMBOLS 317U
#define XPK_LHLB_NODES (XPK_LHLB_SYMBOLS * 2U - 1U)
typedef struct xpk_lhlb_node {
    uint32_t frequency, index, parent, child[2];
} xpk_lhlb_node;
typedef struct xpk_lhlb_tree {
    xpk_lhlb_node node[XPK_LHLB_NODES];
    uint32_t map[XPK_LHLB_NODES];
} xpk_lhlb_tree;
typedef struct xpk_lhlb_bits {
    const uint8_t *data;
    size_t size, at;
    uint16_t word;
    unsigned left;
} xpk_lhlb_bits;
static bool xpk_lhlb_read(xpk_lhlb_bits *bits, unsigned count, uint32_t *value)
{
    uint32_t result = 0U;
    unsigned i;
    if (count > 16U) return false;
    for (i = 0U; i < count; ++i) {
        if (!bits->left) {
            if (bits->at > bits->size || bits->size - bits->at < 2U) return false;
            bits->word = (uint16_t)(((uint16_t)bits->data[bits->at] << 8U) | bits->data[bits->at + 1U]);
            bits->at += 2U;
            bits->left = 16U;
        }
        --bits->left;
        result = (result << 1U) | ((bits->word >> bits->left) & 1U);
    }
    *value = result;
    return true;
}
static void xpk_lhlb_tree_init(xpk_lhlb_tree *tree)
{
    uint32_t i, j;
    for (i = 0U; i < XPK_LHLB_SYMBOLS; ++i) {
        tree->node[i].frequency = 1U;
        tree->node[i].index = i;
        tree->node[i].parent = XPK_LHLB_SYMBOLS + (i >> 1U);
        tree->node[i].child[0] = tree->node[i].child[1] = 0U;
        tree->map[i] = i;
    }
    for (i = XPK_LHLB_SYMBOLS, j = 0U; i < XPK_LHLB_NODES; ++i, j += 2U) {
        tree->node[i].frequency = tree->node[j].frequency + tree->node[j + 1U].frequency;
        tree->node[i].index = i;
        tree->node[i].parent = XPK_LHLB_SYMBOLS + (i >> 1U);
        tree->node[i].child[0] = j;
        tree->node[i].child[1] = j + 1U;
        tree->map[i] = i;
    }
}
static bool xpk_lhlb_symbol(const xpk_lhlb_tree *tree, xpk_lhlb_bits *bits, uint32_t *symbol)
{
    uint32_t at = XPK_LHLB_NODES - 1U, depth = 0U;
    while (at >= XPK_LHLB_SYMBOLS) {
        uint32_t bit;
        if (depth++ >= XPK_LHLB_NODES || !xpk_lhlb_read(bits, 1U, &bit)) return false;
        at = tree->node[at].child[bit];
        if (at >= XPK_LHLB_NODES) return false;
    }
    *symbol = at;
    return true;
}
static bool xpk_lhlb_update(xpk_lhlb_tree *tree, uint32_t code)
{
    while (code != XPK_LHLB_NODES - 1U) {
        uint32_t index, destination, other, frequency;
        if (code >= XPK_LHLB_NODES) return false;
        frequency = ++tree->node[code].frequency;
        index = tree->node[code].index;
        destination = index;
        if (index >= XPK_LHLB_NODES) return false;
        while (destination != XPK_LHLB_NODES - 1U && frequency > tree->node[tree->map[destination + 1U]].frequency) ++destination;
        if (index != destination) {
            uint32_t *left, *right, temp;
            other = tree->map[destination];
            if (other >= XPK_LHLB_NODES || tree->node[code].parent >= XPK_LHLB_NODES || tree->node[other].parent >= XPK_LHLB_NODES) return false;
            left = tree->node[tree->node[code].parent].child;
            right = tree->node[tree->node[other].parent].child;
            left = &left[left[0] == code ? 0 : 1];
            right = &right[right[0] == other ? 0 : 1];
            temp = tree->node[code].index;
            tree->node[code].index = tree->node[other].index;
            tree->node[other].index = temp;
            temp = tree->map[index];
            tree->map[index] = tree->map[destination];
            tree->map[destination] = temp;
            temp = *left;
            *left = *right;
            *right = temp;
            temp = tree->node[code].parent;
            tree->node[code].parent = tree->node[other].parent;
            tree->node[other].parent = temp;
        }
        code = tree->node[code].parent;
    }
    ++tree->node[code].frequency;
    return true;
}
/* Version 28=LHLB. */
static bool xpk_lhlb_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    static const uint8_t lengths[16] = {5U, 5U, 6U, 6U, 6U, 7U, 7U, 7U, 7U, 8U, 8U, 8U, 9U, 9U, 9U, 10U};
    uint32_t offsets[16];
    xpk_lhlb_bits bits;
    xpk_lhlb_tree tree;
    size_t produced = 0U;
    unsigned i;
    if (!packed || !output || !wanted || xx_pd_is_stopped(pd)) return false;
    bits.data = packed;
    bits.size = size;
    bits.at = 0U;
    bits.word = 0U;
    bits.left = 0U;
    offsets[0] = 0U;
    for (i = 1U; i < 16U; ++i) offsets[i] = offsets[i - 1U] + (1U << lengths[i - 1U]);
    xpk_lhlb_tree_init(&tree);
    while (produced < wanted) {
        uint32_t code;
        if (xx_pd_is_stopped(pd) || !xpk_lhlb_symbol(&tree, &bits, &code)) return false;
        if (code == 316U) break;
        if (tree.node[XPK_LHLB_NODES - 1U].frequency < 0x8000U && !xpk_lhlb_update(&tree, code)) return false;
        if (code < 256U) {
            output[produced++] = (uint8_t)code;
        } else {
            uint32_t selector, distance, count, extra;
            size_t j;
            if (!xpk_lhlb_read(&bits, 4U, &selector) || selector >= 16U || !xpk_lhlb_read(&bits, lengths[selector], &extra)) return false;
            distance = offsets[selector] + extra;
            count = code - 255U;
            if (count > wanted - produced || (distance && distance > produced)) return false;
            if (!distance) memset(output + produced, 0, count);
            else
                for (j = 0U; j < count; ++j) {
                    if ((j & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
                    output[produced + j] = output[produced + j - distance];
                }
            produced += count;
        }
    }
    if (produced < wanted) memset(output + produced, 0, wanted - produced);
    return !xx_pd_is_stopped(pd);
}
#endif
