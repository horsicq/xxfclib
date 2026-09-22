/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native ARCV v4 method-2 decoder.  The tree intentionally begins with a
 * weight of one at every non-root node; treating internal nodes as sums at
 * startup changes the first update and desynchronises the stream.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/arcv4/xx_arcv4.h"

#include <string.h>

#define ARCV4_SYMBOLS 3245U
#define ARCV4_NODES (ARCV4_SYMBOLS * 2U - 1U)
#define ARCV4_ROOT 1U
#define ARCV4_END_SYMBOL 256U
#define ARCV4_FIRST_MATCH 257U
#define ARCV4_LENGTH_SPAN 498U
#define ARCV4_MIN_LENGTH 3U
#define ARCV4_BUCKETS 6U
#define ARCV4_MAX_ROOT_FREQ 2000U

typedef struct arcv4_model_s {
    uint16_t frequency[ARCV4_NODES + 1U];
    uint16_t parent[ARCV4_NODES + 1U];
    uint16_t child0[ARCV4_NODES + 1U];
    uint16_t child1[ARCV4_NODES + 1U];
} arcv4_model;

typedef struct arcv4_bits_s {
    const uint8_t *input;
    size_t input_size;
    size_t position;
    uint8_t buffer;
    uint8_t bits_left;
} arcv4_bits;

static bool arcv4_node_valid(uint32_t node) {
    return node >= ARCV4_ROOT && node <= ARCV4_NODES;
}

static void arcv4_model_init(arcv4_model *model) {
    uint32_t index;
    xx_rt_memset(model, 0, sizeof(*model));
    for (index = 2U; index <= ARCV4_NODES; ++index) {
        model->frequency[index] = 1U;
        model->parent[index] = (uint16_t)(index >> 1U);
    }
    for (index = 1U; index < ARCV4_SYMBOLS; ++index) {
        model->child0[index] = (uint16_t)(index * 2U);
        model->child1[index] = (uint16_t)(index * 2U + 1U);
    }
}

static int arcv4_read_bit(arcv4_bits *bits) {
    int result;
    if (!bits) return -1;
    if (bits->bits_left == 0U) {
        if (bits->position >= bits->input_size) return -1;
        bits->buffer = bits->input[bits->position++];
        bits->bits_left = 8U;
    }
    result = bits->buffer & 1U;
    bits->buffer >>= 1U;
    --bits->bits_left;
    return result;
}

static int arcv4_read_bits(arcv4_bits *bits, unsigned count) {
    unsigned index;
    int value = 0;
    if (!bits || count > 16U) return -1;
    for (index = 0U; index < count; ++index) {
        int bit = arcv4_read_bit(bits);
        if (bit < 0) return -1;
        if (bit != 0) value |= 1 << index;
    }
    return value;
}

static bool arcv4_propagate(arcv4_model *model, uint32_t node,
                            uint32_t sibling) {
    uint32_t step;
    if (!model) return false;
    for (step = 0U; step <= ARCV4_NODES; ++step) {
        uint32_t child = node;
        uint32_t parent;
        uint32_t grand;
        if (!arcv4_node_valid(child) || !arcv4_node_valid(sibling)) return false;
        parent = model->parent[child];
        if (!arcv4_node_valid(parent)) return false;
        model->frequency[parent] = (uint16_t)(model->frequency[child] +
                                              model->frequency[sibling]);
        if (parent == ARCV4_ROOT) {
            if (model->frequency[ARCV4_ROOT] == ARCV4_MAX_ROOT_FREQ) {
                uint32_t index;
                for (index = ARCV4_ROOT; index <= ARCV4_NODES; ++index)
                    model->frequency[index] >>= 1U;
            }
            return true;
        }
        grand = model->parent[parent];
        if (!arcv4_node_valid(grand)) return false;
        sibling = model->child0[grand];
        if (sibling == parent) sibling = model->child1[grand];
        if (!arcv4_node_valid(sibling)) return false;
        node = parent;
    }
    return false;
}

static bool arcv4_update(arcv4_model *model, uint32_t node) {
    uint32_t parent;
    uint32_t sibling;
    uint32_t step;
    if (!model || !arcv4_node_valid(node)) return false;
    ++model->frequency[node];
    parent = model->parent[node];
    if (!arcv4_node_valid(parent)) return false;
    if (parent == ARCV4_ROOT) return true;
    sibling = model->child0[parent];
    if (sibling == node) sibling = model->child1[parent];
    if (!arcv4_node_valid(sibling) || !arcv4_propagate(model, node, sibling))
        return false;

    for (step = 0U; step <= ARCV4_NODES; ++step) {
        uint32_t grand = model->parent[parent];
        uint32_t left;
        uint32_t uncle;
        if (!arcv4_node_valid(grand)) return false;
        left = model->child0[grand];
        uncle = parent == left ? model->child1[grand] : left;
        if (!arcv4_node_valid(uncle)) return false;
        if (model->frequency[uncle] < model->frequency[node]) {
            uint32_t node_sibling;
            if (parent == left)
                model->child1[grand] = (uint16_t)node;
            else
                model->child0[grand] = (uint16_t)node;
            node_sibling = model->child0[parent];
            if (node == node_sibling) {
                node_sibling = model->child1[parent];
                model->child0[parent] = (uint16_t)uncle;
            } else {
                model->child1[parent] = (uint16_t)uncle;
            }
            if (!arcv4_node_valid(node_sibling)) return false;
            model->parent[uncle] = (uint16_t)parent;
            model->parent[node] = (uint16_t)grand;
            if (!arcv4_propagate(model, uncle, node_sibling)) return false;
            node = uncle;
        }
        node = model->parent[node];
        if (!arcv4_node_valid(node)) return false;
        parent = model->parent[node];
        if (!arcv4_node_valid(parent)) return false;
        if (parent == ARCV4_ROOT) return true;
    }
    return false;
}

static int arcv4_decode_symbol(arcv4_model *model, arcv4_bits *bits) {
    uint32_t node = ARCV4_ROOT;
    uint32_t step;
    if (!model || !bits) return -2;
    for (step = 0U; step <= ARCV4_NODES; ++step) {
        int bit = arcv4_read_bit(bits);
        if (bit < 0) return -1;
        node = bit == 0 ? model->child0[node] : model->child1[node];
        if (!arcv4_node_valid(node)) return -2;
        if (node >= ARCV4_SYMBOLS) {
            if (!arcv4_update(model, node)) return -2;
            return (int)(node - ARCV4_SYMBOLS);
        }
    }
    return -2;
}

bool xx_arcv4_decode_memory(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size,
                            size_t *written) {
    static const uint16_t extra_bits[ARCV4_BUCKETS] = { 4U, 6U, 8U,
                                                         10U, 12U, 14U };
    static const uint16_t base_distance[ARCV4_BUCKETS] = { 0U, 16U, 80U,
                                                            336U, 1360U, 5456U };
    arcv4_model model;
    arcv4_bits bits;
    size_t produced = 0U;
    bool result = false;
    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        input_size == 0U)
        return false;
    xx_rt_memset(&bits, 0, sizeof(bits));
    bits.input = input;
    bits.input_size = input_size;
    arcv4_model_init(&model);
    for (;;) {
        int symbol = arcv4_decode_symbol(&model, &bits);
        if (symbol < 0) break;
        if ((unsigned)symbol == ARCV4_END_SYMBOL) {
            result = true;
            break;
        }
        if ((unsigned)symbol < ARCV4_END_SYMBOL) {
            if (produced >= output_size) break;
            output[produced++] = (uint8_t)symbol;
        } else {
            unsigned code = (unsigned)symbol - ARCV4_FIRST_MATCH;
            unsigned length = code % ARCV4_LENGTH_SPAN + ARCV4_MIN_LENGTH;
            unsigned bucket = code / ARCV4_LENGTH_SPAN;
            int extra;
            size_t distance;
            size_t source;
            unsigned index;
            if (bucket >= ARCV4_BUCKETS || length > output_size - produced)
                break;
            extra = arcv4_read_bits(&bits, extra_bits[bucket]);
            if (extra < 0) break;
            distance = (size_t)base_distance[bucket] + (unsigned)extra + length;
            if (distance > produced) break;
            source = produced - distance;
            for (index = 0U; index < length; ++index)
                output[produced + index] = output[source + index];
            produced += length;
        }
    }
    if (written) *written = produced;
    return result && produced == output_size;
}
