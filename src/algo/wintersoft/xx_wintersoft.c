/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Wintersoft "**++" AHUFF - Nelson-style adaptive Huffman, ported from the
 * reference decoder.  See xx_wintersoft.h for the format and for why the
 * container's other codec (LZW15V) is not duplicated here.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/wintersoft/xx_wintersoft.h"

#define XX_WS_ROOT 0
#define XX_WS_MAX_WEIGHT 0x8000
#define XX_WS_EOS 0x100
#define XX_WS_ESC 0x101
#define XX_WS_SYMBOLS 0x102
/* 2 * XX_WS_SYMBOLS: one spare pair over the 0x203 slots a full alphabet can
 * actually reach. */
#define XX_WS_NODES 0x204

/* One byte at a time, MSB first.  Running dry is a hard stop: unlike the WPK
 * reader there is no zero padding here. */
typedef struct xx_ws_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t current;
    int32_t count;
} xx_ws_bits;

static void xx_ws_bits_init(xx_ws_bits *bits, const uint8_t *data,
                            size_t size) {
    bits->data = data;
    bits->size = size;
    bits->position = 0;
    bits->current = 0;
    bits->count = 0;
}

static bool xx_ws_bits_bit(xx_ws_bits *bits, int32_t *value) {
    if (bits->count == 0) {
        if (bits->position >= bits->size) {
            return false;
        }
        bits->current = bits->data[bits->position];
        bits->position++;
        bits->count = 8;
    }
    *value = (int32_t)((bits->current >> 7) & 1U);
    bits->current = (uint8_t)(bits->current << 1);
    bits->count--;

    return true;
}

/* The tree as parallel arrays sorted by descending weight.  The parent link is
 * deliberately NOT part of a swap - a node keeps the parent belonging to its
 * SLOT, not to its contents, which is what makes the sibling property repair
 * work in place.  All of this lives in the caller's frame: no static state. */
typedef struct xx_ws_tree_s {
    int32_t leaf[XX_WS_SYMBOLS];
    int32_t weight[XX_WS_NODES];
    int32_t parent[XX_WS_NODES];
    int32_t child[XX_WS_NODES];
    uint8_t is_leaf[XX_WS_NODES];
    int32_t next_free;
} xx_ws_tree;

static void xx_ws_tree_init(xx_ws_tree *tree) {
    int32_t i;

    for (i = 0; i < XX_WS_SYMBOLS; i++) {
        tree->leaf[i] = -1;
    }
    for (i = 0; i < XX_WS_NODES; i++) {
        tree->weight[i] = 0;
        tree->parent[i] = 0;
        tree->child[i] = 0;
        tree->is_leaf[i] = 0;
    }

    tree->weight[0] = 2;
    tree->child[0] = 1;
    tree->is_leaf[0] = 0;
    tree->parent[0] = -1;

    tree->weight[1] = 1;
    tree->child[1] = XX_WS_EOS;
    tree->is_leaf[1] = 1;
    tree->parent[1] = 0;
    tree->leaf[XX_WS_EOS] = 1;

    tree->weight[2] = 1;
    tree->child[2] = XX_WS_ESC;
    tree->is_leaf[2] = 1;
    tree->parent[2] = 0;
    tree->leaf[XX_WS_ESC] = 2;

    tree->next_free = 3;
}

/* The source is always a leaf in practice, but a corrupt stream must not be
 * able to turn a node index into a write outside leaf[]. */
static void xx_ws_set_leaf_slot(xx_ws_tree *tree, int32_t symbol,
                                int32_t node) {
    if ((symbol >= 0) && (symbol < XX_WS_SYMBOLS)) {
        tree->leaf[symbol] = node;
    }
}

static void xx_ws_relink(xx_ws_tree *tree, int32_t node, int32_t target) {
    if (tree->is_leaf[node]) {
        xx_ws_set_leaf_slot(tree, tree->child[node], target);
    } else {
        const int32_t child = tree->child[node];
        if ((child >= 0) && ((child + 1) < XX_WS_NODES)) {
            tree->parent[child] = target;
            tree->parent[child + 1] = target;
        }
    }
}

static void xx_ws_swap_nodes(xx_ws_tree *tree, int32_t first, int32_t second) {
    int32_t weight;
    int32_t child;
    uint8_t is_leaf;

    xx_ws_relink(tree, first, second);
    xx_ws_relink(tree, second, first);

    weight = tree->weight[first];
    child = tree->child[first];
    is_leaf = tree->is_leaf[first];

    tree->weight[first] = tree->weight[second];
    tree->child[first] = tree->child[second];
    tree->is_leaf[first] = tree->is_leaf[second];

    tree->weight[second] = weight;
    tree->child[second] = child;
    tree->is_leaf[second] = is_leaf;
    /* Parents stay with the slots.  Deliberate. */
}

/* Split the lightest node - always the ESCAPE leaf - to make room for a symbol
 * seen for the first time. */
static bool xx_ws_tree_add(xx_ws_tree *tree, int32_t symbol) {
    int32_t light;
    int32_t fresh;
    int32_t zero;

    if ((symbol < 0) || (symbol >= XX_WS_SYMBOLS)) {
        return false;
    }
    if ((tree->next_free + 1) >= XX_WS_NODES) {
        return false;
    }

    light = tree->next_free - 1;
    fresh = tree->next_free;
    zero = tree->next_free + 1;
    tree->next_free += 2;

    tree->weight[fresh] = tree->weight[light];
    tree->parent[fresh] = light;
    tree->child[fresh] = tree->child[light];
    tree->is_leaf[fresh] = tree->is_leaf[light];
    xx_ws_set_leaf_slot(tree, tree->child[fresh], fresh);

    tree->child[light] = fresh;
    tree->is_leaf[light] = 0;

    tree->child[zero] = symbol;
    tree->is_leaf[zero] = 1;
    tree->weight[zero] = 0;
    tree->parent[zero] = light;
    tree->leaf[symbol] = zero;

    return true;
}

/* Nelson RebuildTree: pack the leaves to the end with halved weights, then
 * rebuild the internal nodes back down to the root.  Fires only once the root
 * weight reaches 0x8000, so it is the least travelled path here. */
static void xx_ws_tree_rebuild(xx_ws_tree *tree) {
    int32_t destination = tree->next_free - 1;
    int32_t source;
    int32_t i;

    for (i = tree->next_free - 1; i >= XX_WS_ROOT; i--) {
        if (tree->is_leaf[i]) {
            /* (w + 1) >> 1, not w >> 1: a weight of 1 must stay 1. */
            tree->weight[destination] = (tree->weight[i] + 1) >> 1;
            tree->parent[destination] = tree->parent[i];
            tree->child[destination] = tree->child[i];
            tree->is_leaf[destination] = tree->is_leaf[i];
            destination--;
        }
    }

    source = tree->next_free - 2;
    while (destination >= XX_WS_ROOT) {
        int32_t weight;
        int32_t slot;

        if ((source < 0) || ((source + 1) >= XX_WS_NODES)) {
            return;
        }

        weight = tree->weight[source] + tree->weight[source + 1];
        tree->weight[destination] = weight;
        tree->is_leaf[destination] = 0;

        slot = destination + 1;
        while ((slot < XX_WS_NODES) && (weight < tree->weight[slot])) {
            slot++;
        }
        slot--;

        for (i = destination; i < slot; i++) {
            tree->weight[i] = tree->weight[i + 1];
            tree->parent[i] = tree->parent[i + 1];
            tree->child[i] = tree->child[i + 1];
            tree->is_leaf[i] = tree->is_leaf[i + 1];
        }

        tree->weight[slot] = weight;
        tree->child[slot] = source;
        tree->is_leaf[slot] = 0;

        source -= 2;
        destination--;
    }

    for (i = tree->next_free - 1; i >= XX_WS_ROOT; i--) {
        xx_ws_relink(tree, i, i);
    }
}

static void xx_ws_tree_update(xx_ws_tree *tree, int32_t symbol) {
    int32_t current;

    if (tree->weight[XX_WS_ROOT] == XX_WS_MAX_WEIGHT) {
        xx_ws_tree_rebuild(tree);
    }
    if ((symbol < 0) || (symbol >= XX_WS_SYMBOLS)) {
        return;
    }

    current = tree->leaf[symbol];
    while (current != -1) {
        int32_t fresh;

        if ((current < 0) || (current >= XX_WS_NODES)) {
            return;
        }
        tree->weight[current]++;

        fresh = current;
        while (fresh > XX_WS_ROOT) {
            if (tree->weight[fresh - 1] >= tree->weight[current]) {
                break;
            }
            fresh--;
        }
        if (current != fresh) {
            xx_ws_swap_nodes(tree, current, fresh);
            current = fresh;
        }
        current = tree->parent[current];
    }
}

bool xx_wintersoft_ahuff_decode_memory(const uint8_t *input, size_t input_size,
                                       uint8_t *output, size_t output_size,
                                       size_t *written) {
    xx_ws_bits bits;
    xx_ws_tree tree;
    size_t produced = 0;
    bool ok = true;

    if (written) {
        *written = 0;
    }
    if (!input && (input_size != 0)) {
        return false;
    }
    if (!output || (output_size == 0)) {
        return false;
    }

    xx_ws_bits_init(&bits, input, input_size);
    xx_ws_tree_init(&tree);

    while (produced < output_size) {
        int32_t node = XX_WS_ROOT;
        int32_t depth = 0;
        int32_t symbol;
        bool stop = false;

        while (!tree.is_leaf[node]) {
            int32_t bit = 0;

            depth++;
            if (depth > XX_WS_NODES) {
                stop = true;
                break;
            }
            node = tree.child[node];
            if (!xx_ws_bits_bit(&bits, &bit)) {
                stop = true;
                break;
            }
            node += bit;
            if ((node < 0) || (node >= XX_WS_NODES)) {
                stop = true;
                break;
            }
        }
        if (stop) {
            ok = false;
            break;
        }

        symbol = tree.child[node];
        if (symbol == XX_WS_EOS) {
            /* END before the stored size is reached: the member is short, and
             * a short member is a failure, never a truncation. */
            ok = false;
            break;
        }

        if (symbol == XX_WS_ESC) {
            /* A byte never seen before: eight raw bits, then the escape leaf
             * is split so the byte gets a leaf of its own. */
            int32_t i;

            symbol = 0;
            for (i = 0; i < 8; i++) {
                int32_t bit = 0;
                if (!xx_ws_bits_bit(&bits, &bit)) {
                    stop = true;
                    break;
                }
                symbol = symbol * 2 + bit;
            }
            if (stop) {
                ok = false;
                break;
            }
            if (!xx_ws_tree_add(&tree, symbol)) {
                ok = false;
                break;
            }
        }

        output[produced] = (uint8_t)(symbol & 0xFF);
        produced++;
        xx_ws_tree_update(&tree, symbol);
    }

    if (ok && (produced == output_size)) {
        if (written) {
            *written = produced;
        }

        return true;
    }

    return false;
}

bool xx_wintersoft_decode_memory(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_size,
                                 size_t *written) {
    return xx_wintersoft_ahuff_decode_memory(input, input_size, output,
                                             output_size, written);
}
