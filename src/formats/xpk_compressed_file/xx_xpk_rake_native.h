/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * Copyright (c) 2017-2026 Teemu Suutari
 * SPDX-License-Identifier: MIT AND BSD-2-Clause
 *
 * Bounded native C adaptation of Ancient's RAKEDecompressor.cpp, revision
 * 61cd9088a218ce43fd389f3f3c48f35fc70f7834. See LICENSE.ancient.
 * RAKE and FRHT share the same two-stream, backwards-output wire format.
 */
#ifndef XX_XPK_RAKE_NATIVE_H
#define XX_XPK_RAKE_NATIVE_H

#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>

#include "xx_xpk_rake_table.h"
#include "xxfclib/data/xx_data.h"

typedef struct xpk_rake_node_s {
    uint16_t child[2];
    uint8_t symbol;
    bool terminal;
} xpk_rake_node;

typedef struct xpk_rake_bits_s {
    const uint8_t *data;
    size_t cursor, size;
    uint32_t word;
    unsigned available;
} xpk_rake_bits;

static bool xpk_rake_bit(xpk_rake_bits *bits, uint32_t *value)
{
    if (!bits->available) {
        if (bits->cursor > bits->size || bits->size - bits->cursor < 4U) return false;
        bits->word = xx_data_get_u32(bits->data + bits->cursor, 4, 0, true);
        bits->cursor += 4U;
        bits->available = 32U;
    }
    --bits->available;
    *value = (bits->word >> bits->available) & 1U;
    return true;
}

static bool xpk_rake_read_bits(xpk_rake_bits *bits, unsigned count, uint32_t *value)
{
    uint32_t result = 0U, bit;
    unsigned i;
    for (i = 0U; i < count; ++i) {
        if (!xpk_rake_bit(bits, &bit)) return false;
        result = (result << 1U) | bit;
    }
    *value = result;
    return true;
}

static bool xpk_rake_tree(xpk_rake_node *nodes)
{
    uint32_t code = 0U;
    unsigned used = 1U, i;
    for (i = 0U; i < sizeof(xpk_rake_symbols) / sizeof(xpk_rake_symbols[0]); ++i) {
        unsigned depth = xpk_rake_symbols[i][0], j, current = 0U;
        if (!depth || depth > 19U) return false;
        for (j = depth; j != 0U; --j) {
            unsigned side = (code >> (32U - depth + j - 1U)) & 1U;
            uint16_t child = nodes[current].child[side];
            if (!child) {
                if (used >= 512U) return false;
                child = (uint16_t)used++;
                nodes[current].child[side] = child;
            }
            current = child;
            if (j != 1U && nodes[current].terminal) return false;
        }
        if (nodes[current].terminal || nodes[current].child[0] || nodes[current].child[1]) return false;
        nodes[current].symbol = xpk_rake_symbols[i][1];
        nodes[current].terminal = true;
        code += UINT32_C(1) << (32U - depth);
    }
    return true;
}

static bool xpk_rake_symbol(xpk_rake_bits *bits, const xpk_rake_node *nodes, uint32_t *value)
{
    unsigned current = 0U, depth;
    for (depth = 0U; depth < 19U; ++depth) {
        uint32_t bit;
        if (!xpk_rake_bit(bits, &bit)) return false;
        current = nodes[current].child[bit];
        if (!current) return false;
        if (nodes[current].terminal) {
            *value = nodes[current].symbol;
            return true;
        }
    }
    return false;
}

static bool xpk_rake_native(const uint8_t *packed, size_t size, uint8_t *output, size_t wanted, xx_pd_struct *pd)
{
    xpk_rake_node nodes[512] = {0};
    xpk_rake_bits bits;
    size_t middle, back, position = wanted, operations = 0U;
    uint32_t pad, value;
    if (!packed || (wanted && !output) || size < 8U || xx_pd_is_stopped(pd)) return false;
    pad = ((uint32_t)packed[0] << 8U) | packed[1];
    middle = ((size_t)packed[2] << 8U) | packed[3];
    if (pad > 32U || middle < 4U || middle >= size || middle + (middle & 1U) > size || size - (middle + (middle & 1U)) < 4U || !xpk_rake_tree(nodes)) return false;
    back = middle;
    bits.data = packed;
    bits.size = size;
    bits.cursor = middle + (middle & 1U);
    bits.word = xx_data_get_u32(packed + bits.cursor, 4, 0, true);
    bits.cursor += 4U;
    bits.word = pad == 32U ? 0U : bits.word >> pad;
    bits.available = 32U - pad;
    while (position) {
        if (((operations++) & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
        if (!xpk_rake_bit(&bits, &value)) return false;
        if (!value) {
            if (back <= 4U) return false;
            output[--position] = packed[--back];
        } else {
            uint32_t count, distance, bit;
            size_t i;
            if (!xpk_rake_symbol(&bits, nodes, &count) || !xpk_rake_bit(&bits, &bit)) return false;
            count += 2U;
            if (!bit) {
                if (back <= 4U) return false;
                distance = (uint32_t)packed[--back] + 1U;
            } else {
                if (!xpk_rake_bit(&bits, &bit)) return false;
                if (!xpk_rake_read_bits(&bits, bit ? 6U : 3U, &distance) || back <= 4U) return false;
                distance = (distance << 8U) | packed[--back];
                distance += bit ? 0x901U : 0x101U;
            }
            if (count > position || distance > wanted - position || !distance) return false;
            for (i = 0U; i < count; ++i) {
                if ((i & 1023U) == 0U && xx_pd_is_stopped(pd)) return false;
                --position;
                output[position] = output[position + distance];
            }
        }
    }
    return !xx_pd_is_stopped(pd);
}
#endif
