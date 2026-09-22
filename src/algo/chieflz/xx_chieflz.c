/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native ChiefLZ method-4 decoder, ported from XChiefLZDecoder.
 *
 * As in ARCV v4, the tree intentionally begins with a weight of one at every
 * non-root node; treating internal nodes as sums of their children at startup
 * changes the very first update and desynchronises the stream.  The weight
 * halving triggers on the root reaching EXACTLY 2000 (not >=): that is the
 * encoder's own test and must not be "fixed" to a comparison.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/chieflz/xx_chieflz.h"

#define CHIEFLZ_SYMBOLS 629U
#define CHIEFLZ_NODES (CHIEFLZ_SYMBOLS * 2U - 1U) /* 1257 */
#define CHIEFLZ_ROOT 1U
#define CHIEFLZ_END_SYMBOL 256U
#define CHIEFLZ_FIRST_MATCH 257U
#define CHIEFLZ_LENGTH_SPAN 62U
#define CHIEFLZ_MIN_LENGTH 3U
#define CHIEFLZ_BUCKETS 6U
#define CHIEFLZ_MAX_ROOT_FREQ 2000U

typedef struct chieflz_model_s {
    uint16_t frequency[CHIEFLZ_NODES + 1U];
    uint16_t parent[CHIEFLZ_NODES + 1U];
    uint16_t child0[CHIEFLZ_NODES + 1U];
    uint16_t child1[CHIEFLZ_NODES + 1U];
} chieflz_model;

/* MSB-first out of a little-endian 16 bit word. */
typedef struct chieflz_bits_s {
    const uint8_t *input;
    size_t input_size;
    size_t position;
    uint16_t word;
    unsigned bits_left;
} chieflz_bits;

static bool chieflz_node_valid(uint32_t node) {
    return node >= CHIEFLZ_ROOT && node <= CHIEFLZ_NODES;
}

static void chieflz_model_init(chieflz_model *model) {
    uint32_t index;
    xx_rt_memset(model, 0, sizeof(*model));
    for (index = 2U; index <= CHIEFLZ_NODES; ++index) {
        model->frequency[index] = 1U;
        model->parent[index] = (uint16_t)(index >> 1U);
    }
    for (index = 1U; index < CHIEFLZ_SYMBOLS; ++index) {
        model->child0[index] = (uint16_t)(index * 2U);
        model->child1[index] = (uint16_t)(index * 2U + 1U);
    }
}

static int chieflz_read_bit(chieflz_bits *bits) {
    int result;
    if (!bits) return -1;
    if (bits->bits_left == 0U) {
        /* A partial trailing byte is never a valid refill: the encoder always
         * emits whole 16 bit words. */
        if (bits->position + 2U > bits->input_size) return -1;
        bits->word = (uint16_t)((uint16_t)bits->input[bits->position] |
                                ((uint16_t)bits->input[bits->position + 1U]
                                 << 8U));
        bits->position += 2U;
        bits->bits_left = 16U;
    }
    result = (int)((bits->word >> 15U) & 1U);
    bits->word = (uint16_t)(bits->word << 1U);
    --bits->bits_left;
    return result;
}

/* The value is assembled LSB-first from bits that arrive MSB-first.  This
 * looks like a bug and is not: it is what the encoder does. */
static int chieflz_read_bits(chieflz_bits *bits, unsigned count) {
    unsigned index;
    int value = 0;
    if (!bits || count > 16U) return -1;
    for (index = 0U; index < count; ++index) {
        int bit = chieflz_read_bit(bits);
        if (bit < 0) return -1;
        if (bit != 0) value |= 1 << index;
    }
    return value;
}

/* Re-sum weights from `node` up to the root, then halve everything if the
 * root has just reached the ceiling. */
static bool chieflz_propagate(chieflz_model *model, uint32_t node,
                              uint32_t sibling) {
    uint32_t step;
    if (!model) return false;
    for (step = 0U; step <= CHIEFLZ_NODES; ++step) {
        uint32_t child = node;
        uint32_t parent;
        uint32_t grand;
        if (!chieflz_node_valid(child) || !chieflz_node_valid(sibling))
            return false;
        parent = model->parent[child];
        if (!chieflz_node_valid(parent)) return false;
        model->frequency[parent] = (uint16_t)(model->frequency[child] +
                                              model->frequency[sibling]);
        if (parent == CHIEFLZ_ROOT) {
            if (model->frequency[CHIEFLZ_ROOT] == CHIEFLZ_MAX_ROOT_FREQ) {
                uint32_t index;
                for (index = CHIEFLZ_ROOT; index <= CHIEFLZ_NODES; ++index)
                    model->frequency[index] >>= 1U;
            }
            return true;
        }
        grand = model->parent[parent];
        if (!chieflz_node_valid(grand)) return false;
        sibling = model->child0[grand];
        if (sibling == parent) sibling = model->child1[grand];
        if (!chieflz_node_valid(sibling)) return false;
        node = parent;
    }
    return false;
}

static bool chieflz_update(chieflz_model *model, uint32_t node) {
    uint32_t parent;
    uint32_t sibling;
    uint32_t step;
    if (!model || !chieflz_node_valid(node)) return false;
    ++model->frequency[node];
    parent = model->parent[node];
    if (!chieflz_node_valid(parent)) return false;
    if (parent == CHIEFLZ_ROOT) return true;
    sibling = model->child0[parent];
    if (sibling == node) sibling = model->child1[parent];
    if (!chieflz_node_valid(sibling) ||
        !chieflz_propagate(model, node, sibling))
        return false;

    for (step = 0U; step <= CHIEFLZ_NODES; ++step) {
        uint32_t grand = model->parent[parent];
        uint32_t left;
        uint32_t uncle;
        if (!chieflz_node_valid(grand)) return false;
        left = model->child0[grand];
        uncle = parent == left ? model->child1[grand] : left;
        if (!chieflz_node_valid(uncle)) return false;
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
            if (!chieflz_node_valid(node_sibling)) return false;
            model->parent[uncle] = (uint16_t)parent;
            model->parent[node] = (uint16_t)grand;
            if (!chieflz_propagate(model, uncle, node_sibling)) return false;
            node = uncle;
        }
        node = model->parent[node];
        if (!chieflz_node_valid(node)) return false;
        parent = model->parent[node];
        if (!chieflz_node_valid(parent)) return false;
        if (parent == CHIEFLZ_ROOT) return true;
    }
    return false;
}

/* -1: out of input.  -2: malformed tree. */
static int chieflz_decode_symbol(chieflz_model *model, chieflz_bits *bits) {
    uint32_t node = CHIEFLZ_ROOT;
    uint32_t step;
    if (!model || !bits) return -2;
    for (step = 0U; step <= CHIEFLZ_NODES; ++step) {
        int bit = chieflz_read_bit(bits);
        if (bit < 0) return -1;
        node = bit == 0 ? model->child0[node] : model->child1[node];
        if (!chieflz_node_valid(node)) return -2;
        if (node >= CHIEFLZ_SYMBOLS) {
            if (!chieflz_update(model, node)) return -2;
            return (int)(node - CHIEFLZ_SYMBOLS);
        }
    }
    return -2;
}

bool xx_chieflz_decode_memory(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size,
                              size_t *written) {
    /* Shared with ARCV v4: extra widths over bases, and the match length is
     * FOLDED INTO the distance (dist = base + extra + length), so a match can
     * never overlap its own output. */
    static const uint16_t extra_bits[CHIEFLZ_BUCKETS] = { 4U, 6U, 8U,
                                                          10U, 12U, 14U };
    static const uint16_t base_distance[CHIEFLZ_BUCKETS] = { 0U, 16U, 80U,
                                                             336U, 1360U,
                                                             5456U };
    chieflz_model model;
    chieflz_bits bits;
    size_t produced = 0U;
    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        input_size == 0U)
        return false;
    xx_rt_memset(&bits, 0, sizeof(bits));
    bits.input = input;
    bits.input_size = input_size;
    chieflz_model_init(&model);

    /* The reference stops as soon as the plaintext length is reached, so the
     * end marker is optional; reaching output_size IS the success condition.
     *
     * The reference keeps a separate 32 KiB window and indexes it modulo the
     * window size.  The largest encodable distance is 5456 + 16383 + 64 =
     * 21903, which is below 32768, so for any in-range back-reference the
     * window byte and output[produced - distance] are provably the same byte.
     * Decoding straight into the caller buffer is therefore output-equivalent
     * and drops the scratch allocation. */
    while (produced < output_size) {
        int symbol = chieflz_decode_symbol(&model, &bits);
        if (symbol < 0) break;
        if ((unsigned)symbol == CHIEFLZ_END_SYMBOL) break;
        if ((unsigned)symbol < CHIEFLZ_END_SYMBOL) {
            output[produced++] = (uint8_t)symbol;
        } else {
            unsigned code = (unsigned)symbol - CHIEFLZ_FIRST_MATCH;
            unsigned length = code % CHIEFLZ_LENGTH_SPAN + CHIEFLZ_MIN_LENGTH;
            unsigned bucket = code / CHIEFLZ_LENGTH_SPAN;
            size_t distance;
            size_t source;
            size_t available;
            unsigned index;
            int extra;
            if (bucket >= CHIEFLZ_BUCKETS) break;
            extra = chieflz_read_bits(&bits, extra_bits[bucket]);
            if (extra < 0) break;
            distance = (size_t)base_distance[bucket] + (unsigned)extra + length;
            /* The reference's zero-filled window would silently yield zeros
             * for a reference that predates the start of the output.  That is
             * a malformed stream, so fail instead of inventing bytes. */
            if (distance > produced) break;
            source = produced - distance;
            /* The reference lets a final match run past the requested length
             * and then truncates, still reporting success.  Copying only what
             * fits reproduces that exactly, because the loop then ends with
             * the buffer full. */
            available = output_size - produced;
            if ((size_t)length > available) length = (unsigned)available;
            for (index = 0U; index < length; ++index)
                output[produced + index] = output[source + index];
            produced += length;
        }
    }

    if (produced != output_size) return false;
    if (written) *written = produced;
    return true;
}
