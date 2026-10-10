/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 * Bounded native C adaptation of Ancient HUFF/HFMNDecompressor.cpp,
 * commit 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 */
#ifndef XX_XPK_HUFFMAN_NATIVE_H
#define XX_XPK_HUFFMAN_NATIVE_H
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

#define XPK_HUFF_MAX_NODES (256U * 32U + 1U)
typedef struct xpk_huff_node {
    int32_t child[2];
    int32_t symbol;
} xpk_huff_node;
typedef struct xpk_huff_tree {
    xpk_huff_node nodes[XPK_HUFF_MAX_NODES];
    uint32_t count;
} xpk_huff_tree;
typedef struct xpk_huff_bits {
    const uint8_t *data;
    size_t at, end;
    uint8_t value;
    unsigned available;
} xpk_huff_bits;
static bool xpk_huff_bit(xpk_huff_bits *s, uint32_t *value)
{
    if (!s->available) {
        if (s->at >= s->end) return false;
        s->value = s->data[s->at++];
        s->available = 8U;
    }
    --s->available;
    *value = (s->value >> s->available) & 1U;
    return true;
}
static bool xpk_huff_bits_read(xpk_huff_bits *s, unsigned count, uint32_t *value)
{
    uint32_t result = 0U, bit;
    unsigned i;
    for (i = 0U; i < count; ++i) {
        if (!xpk_huff_bit(s, &bit)) return false;
        result = (result << 1U) | bit;
    }
    *value = result;
    return true;
}
static void xpk_huff_node_init(xpk_huff_node *node)
{
    node->child[0] = -1;
    node->child[1] = -1;
    node->symbol = -1;
}
static void xpk_huff_tree_init(xpk_huff_tree *tree)
{
    tree->count = 1U;
    xpk_huff_node_init(&tree->nodes[0]);
}
static bool xpk_huff_insert(xpk_huff_tree *tree, unsigned width, uint32_t code, uint32_t symbol)
{
    uint32_t at = 0U;
    unsigned i;
    if (!width || width > 32U || symbol > 255U) return false;
    for (i = 0U; i < width; ++i) {
        uint32_t bit = (code >> (width - i - 1U)) & 1U;
        int32_t next;
        if (tree->nodes[at].symbol >= 0) return false;
        next = tree->nodes[at].child[bit];
        if (next < 0) {
            if (tree->count >= XPK_HUFF_MAX_NODES) return false;
            next = (int32_t)tree->count++;
            tree->nodes[at].child[bit] = next;
            xpk_huff_node_init(&tree->nodes[next]);
        }
        at = (uint32_t)next;
    }
    if (tree->nodes[at].symbol >= 0 || tree->nodes[at].child[0] >= 0 || tree->nodes[at].child[1] >= 0) return false;
    tree->nodes[at].symbol = (int32_t)symbol;
    return true;
}
static bool xpk_huff_decode(xpk_huff_tree *tree, xpk_huff_bits *bits, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    size_t i;
    for (i = 0U; i < wanted; ++i) {
        uint32_t at = 0U, bit;
        unsigned depth = 0U;
        if (xx_pd_is_stopped(pd)) return false;
        while (tree->nodes[at].symbol < 0) {
            int32_t next;
            if (depth++ >= 32U || !xpk_huff_bit(bits, &bit)) return false;
            next = tree->nodes[at].child[bit];
            if (next < 0) return false;
            at = (uint32_t)next;
        }
        output[i] = (uint8_t)tree->nodes[at].symbol;
    }
    return !xx_pd_is_stopped(pd);
}
/* Version 18=HUFF. */
static bool xpk_huff_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    xpk_huff_tree tree;
    xpk_huff_bits bits;
    size_t at = 6U;
    unsigned symbol;
    if (!packed || size < 6U || (wanted && !output) || packed[0] || packed[1] || packed[2] != 0xabU || packed[3] != 0xadU || packed[4] != 0xcaU || packed[5] != 0xfeU ||
        xx_pd_is_stopped(pd))
        return false;
    xpk_huff_tree_init(&tree);
    for (symbol = 0U; symbol < 256U; ++symbol) {
        unsigned width, bytes, i;
        uint32_t code = 0U;
        if (xx_pd_is_stopped(pd) || at >= size) return false;
        width = (uint8_t)(packed[at++] + 1U);
        if (!width) continue;
        if (width > 32U) return false;
        bytes = (width + 7U) / 8U;
        if (bytes > size - at) return false;
        for (i = 0U; i < bytes; ++i) code = (code << 8U) | packed[at++];
        code >>= (bytes * 8U - width);
        if (width < 32U) code &= (UINT32_C(1) << width) - 1U;
        if (!xpk_huff_insert(&tree, width, code, symbol)) return false;
    }
    bits.data = packed;
    bits.at = at;
    bits.end = size;
    bits.value = 0U;
    bits.available = 0U;
    return xpk_huff_decode(&tree, &bits, output, wanted, pd);
}
/* Version 19=HFMN. */
static bool xpk_hfmn_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    xpk_huff_tree tree;
    xpk_huff_bits bits;
    uint32_t code = 1U;
    unsigned width = 1U;
    size_t header_size;
    uint32_t raw_size;
    if (!packed || size < 4U || (wanted && !output) || xx_pd_is_stopped(pd)) return false;
    header_size = ((size_t)packed[0] << 8U) | packed[1];
    if ((header_size & 3U) != 0U) return false;
    header_size &= 0x1ffU;
    if (header_size > size - 4U) return false;
    raw_size = ((uint32_t)packed[header_size + 2U] << 8U) | packed[header_size + 3U];
    if (!raw_size || raw_size != wanted) return false;
    header_size += 4U;
    xpk_huff_tree_init(&tree);
    bits.data = packed;
    bits.at = 2U;
    bits.end = header_size;
    bits.value = 0U;
    bits.available = 0U;
    for (;;) {
        uint32_t branch, literal = 0U, reversed = 0U;
        unsigned i;
        if (xx_pd_is_stopped(pd) || !xpk_huff_bit(&bits, &branch)) return false;
        if (branch) {
            if (width >= 32U) return false;
            code = (code << 1U) | 1U;
            ++width;
            continue;
        }
        if (!xpk_huff_bits_read(&bits, 8U, &literal)) return false;
        for (i = 0U; i < 8U; ++i) reversed = (reversed << 1U) | ((literal >> i) & 1U);
        if (!xpk_huff_insert(&tree, width, code, reversed)) return false;
        while (!(code & 1U) && width) {
            --width;
            code >>= 1U;
        }
        if (!width) break;
        --code;
    }
    bits.at = header_size;
    bits.end = size;
    bits.value = 0U;
    bits.available = 0U;
    return xpk_huff_decode(&tree, &bits, output, wanted, pd);
}
#endif
