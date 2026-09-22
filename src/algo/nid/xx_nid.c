/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "NI" install-set (.NID / .DAT / .PAC) member decoder.  Ported one-for-one
 * from the XArchive reference decoder (XArchive/Algos/xniddecoder.cpp).  The
 * bit reader, the semi-adaptive weight list, the frozen Huffman tree builder
 * and every bound check are reproduced exactly; only the state lives in one
 * caller-owned heap block instead of C++ objects, and the expanded bytes go
 * straight into the caller buffer instead of being flushed out of the ring in
 * 64 KiB pieces.  That flush is provably output-equivalent: the reference
 * writes the ring strictly sequentially and appends [0, 0x10000) on every wrap
 * plus [0, position) at the end of the block, which is exactly the sequence of
 * bytes written, in order.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/nid/xx_nid.h"

#define NID_NSYM 0x101 /* 257 symbols per model */
#define NID_RING_SIZE 0x10000
#define NID_RING_MASK (NID_RING_SIZE - 1)
#define NID_WEIGHT_STEP 0x20
#define NID_WEIGHT_LIMIT UINT32_C(0xffdd)
#define NID_MAX_TREE_DEPTH 0x10
#define NID_FRAME_SIZE 5
#define NID_COUNT_LENGTH 3000
#define NID_COUNT_DISTANCE 5000
#define NID_COUNT_LITERAL 3000

/* LSB-first bit reader over one block payload. */
typedef struct nid_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t buffer;
    int32_t count;
} nid_bits;

/* Semi-adaptive model: a weight ordered symbol list that freezes into a static
 * Huffman tree once max_count symbols have been decoded through it. */
typedef struct nid_model_s {
    int32_t max_count;
    int32_t counter;
    bool use_list;
    int32_t root;
    uint16_t total;
    uint16_t weight[NID_NSYM + 2];
    uint16_t symbol_at[NID_NSYM];
    /* Maintained exactly as the reference maintains it, and — as in the
     * reference — never read.  Kept so the list bookkeeping stays a
     * line-for-line match. */
    uint16_t position_of[NID_NSYM];
    int32_t child0[NID_NSYM];
    int32_t child1[NID_NSYM];
} nid_model;

typedef struct nid_state_s {
    nid_model length;
    nid_model distance;
    nid_model literal;
    uint8_t ring[NID_RING_SIZE];
} nid_state;

static void nid_bits_init(nid_bits *bits, const uint8_t *data, size_t size) {
    bits->data = data;
    bits->size = size;
    bits->position = 0;
    bits->buffer = 0U;
    bits->count = 0;
}

static int32_t nid_bit(nid_bits *bits) {
    int32_t result;

    if (bits->count == 0) {
        if (bits->position >= bits->size) return -1;
        bits->buffer = bits->data[bits->position++];
        bits->count = 8;
    }

    result = (int32_t)(bits->buffer & 1U);
    bits->buffer >>= 1;
    --bits->count;

    return result;
}

static int32_t nid_read_bits(nid_bits *bits, int32_t count) {
    int32_t result = 0;
    int32_t i;

    for (i = 0; i < count; ++i) {
        int32_t bit = nid_bit(bits);
        if (bit < 0) return -1;
        result |= bit << i;
    }

    return result;
}

static void nid_model_init(nid_model *model, int32_t max_count) {
    int32_t i;

    model->max_count = max_count;
    model->counter = 0;
    model->use_list = true;
    model->root = 0;

    for (i = 0; i < NID_NSYM; ++i) {
        model->weight[i + 1] = 1;
        model->symbol_at[i] = (uint16_t)i;
        model->position_of[i] = (uint16_t)i;
        model->child0[i] = 0;
        model->child1[i] = 0;
    }

    model->weight[0] = 0;
    model->weight[NID_NSYM + 1] = 0;
    model->total = (uint16_t)NID_NSYM;
}

static void nid_rescale(nid_model *model, uint32_t initial) {
    uint32_t accumulator = initial;
    int32_t i;

    for (i = 1; i <= NID_NSYM; ++i) {
        model->weight[i] = (uint16_t)((model->weight[i] + 1) >> 1);
        accumulator += model->weight[i];
    }

    model->total = (uint16_t)accumulator;
}

static void nid_update(nid_model *model, int32_t position) {
    uint16_t symbol = model->symbol_at[position];
    uint16_t new_weight;
    int32_t i;

    /* Deliberate: the reference bumps the running total and rescales BEFORE
     * adding the step to the symbol's own weight, so the rescaled symbol
     * weight then receives a full, unhalved step. */
    model->total = (uint16_t)(model->total + NID_WEIGHT_STEP);
    if ((uint32_t)model->total > NID_WEIGHT_LIMIT) nid_rescale(model, (uint32_t)NID_WEIGHT_STEP);

    model->weight[position + 1] = (uint16_t)(model->weight[position + 1] + NID_WEIGHT_STEP);
    new_weight = model->weight[position + 1];

    i = position;
    while ((i >= 1) && (new_weight > model->weight[i])) {
        model->symbol_at[i] = model->symbol_at[i - 1];
        model->weight[i + 1] = model->weight[i];
        model->position_of[model->symbol_at[i - 1]] = (uint16_t)i;
        --i;
    }

    if (position != i) {
        model->weight[i + 1] = new_weight;
        model->symbol_at[i] = symbol;
        model->position_of[symbol] = (uint16_t)i;
    }
}

/* Splits [start, start + count) at the running-weight half point.  Internal
 * nodes are identified by their split index (< NID_NSYM), leaves by
 * symbol + NID_NSYM. */
static int32_t nid_build(nid_model *model, int32_t start, int32_t count, uint32_t total, int32_t depth) {
    int32_t split;
    int32_t left;
    int32_t right;
    int32_t i;
    uint32_t left_weight;

    if (count == 1) return (int32_t)model->symbol_at[start] + NID_NSYM;
    if (depth >= NID_MAX_TREE_DEPTH) return -1;
    if ((count <= 0) || (start < 0) || (start + count > NID_NSYM)) return -1;

    split = start;
    left_weight = 0U;

    if ((total >> 1) != 0U) {
        i = 0;
        for (;;) {
            ++split;
            if (start + i + 1 > NID_NSYM) return -1;
            left_weight += model->weight[start + i + 1];
            ++i;
            if (left_weight >= (total >> 1)) break;
        }
    }

    left = nid_build(model, start, split - start, left_weight, depth + 1);
    if (left < 0) return -1;
    if ((split < 0) || (split >= NID_NSYM)) return -1;
    model->child0[split] = left;

    /* Deliberate: `total - left_weight` is unsigned and the reference lets it
     * wrap when a single weight exceeds the subtree total.  A wrapped total
     * simply drives the scan above past NID_NSYM and fails the build, which is
     * the reference's behaviour too, so the wrap is kept rather than guarded. */
    right = nid_build(model, split, (count + start) - split, total - left_weight, depth + 1);
    /* Deliberate asymmetry with the `left < 0` test above: the reference
     * rejects a right child of 0 as well, because node 0 can never be a valid
     * right subtree here. */
    if (right < 1) return -1;
    model->child1[split] = right;

    return split;
}

static bool nid_build_tree(nid_model *model) {
    int32_t attempt;

    /* A too-deep code forces a weight rescale and a retry; the reference loops
     * here without a bound, but the weights strictly shrink so a handful of
     * rounds always settles it. */
    for (attempt = 0; attempt < 32; ++attempt) {
        int32_t root = nid_build(model, 0, NID_NSYM, (uint32_t)model->total, 0);
        model->root = root;
        if (root >= 0) return true;
        nid_rescale(model, 0U);
    }

    return false;
}

/* Returns the decoded symbol, or -1 on a bit-reader failure / broken tree. */
static int32_t nid_decode_symbol(nid_model *model, nid_bits *bits) {
    int32_t steps;

    if (!model->use_list) {
        int32_t node = model->root;
        for (steps = 0; steps < 64; ++steps) {
            int32_t bit = nid_bit(bits);
            if (bit < 0) return -1;
            if ((node < 0) || (node >= NID_NSYM)) return -1;
            node = (bit == 0) ? model->child0[node] : model->child1[node];
            if (node >= NID_NSYM) return node - NID_NSYM;
        }
        return -1;
    }

    {
        int32_t index = 1;
        uint32_t range = model->total;

        for (steps = 0; steps < 64; ++steps) {
            uint32_t half = range >> 1;
            int32_t scan = index;
            int32_t next = index + 1;
            uint32_t sum = 0U;
            int32_t bit;

            for (;;) {
                if ((scan < 1) || (scan > NID_NSYM)) return -1;
                sum += model->weight[scan];
                next = scan + 1;
                if (sum >= half) break;
                scan = next;
            }

            bit = nid_bit(bits);
            if (bit < 0) return -1;

            if (bit) {
                if (sum > range) return -1;
                sum = range - sum;
                index = next;
            }

            range = sum;
            if ((index < 1) || (index > NID_NSYM)) return -1;

            if (range <= model->weight[index]) {
                int32_t symbol = (int32_t)model->symbol_at[index - 1];
                int32_t counter = model->counter++;
                if (counter < model->max_count) {
                    nid_update(model, index - 1);
                } else {
                    if (!nid_build_tree(model)) return -1;
                    model->use_list = false;
                }
                return symbol;
            }
        }
    }

    return -1;
}

/* Expands one compressed block straight into output + *out_position.  The
 * block may not produce more than (output_size - *out_position) bytes; that is
 * the reference's per-block nRemaining limit. */
static bool nid_decode_block(nid_state *state, const uint8_t *data, size_t size, uint8_t *output, size_t output_size, size_t *out_position) {
    nid_bits bits;
    int32_t magic;
    int32_t mode;
    uint32_t position = 0U;
    size_t produced = *out_position;

    nid_bits_init(&bits, data, size);
    magic = nid_read_bits(&bits, 3);
    if (magic == 6) {
        mode = 1;
    } else if (magic == 7) {
        mode = 2;
    } else {
        return false;
    }

    nid_model_init(&state->length, NID_COUNT_LENGTH);
    nid_model_init(&state->distance, NID_COUNT_DISTANCE);
    nid_model_init(&state->literal, NID_COUNT_LITERAL);
    xx_rt_memset(state->ring, 0, NID_RING_SIZE);

    for (;;) {
        int32_t symbol = nid_decode_symbol(&state->length, &bits);
        int32_t length;

        if (symbol < 0) return false;
        length = symbol + 2;

        if (length > 0x101) {
            if (length == 0x102) break;
            return false;
        }

        if (length == 0x101) {
            int32_t byte = (mode == 2) ? nid_decode_symbol(&state->literal, &bits) : nid_read_bits(&bits, 8);
            if ((byte < 0) || (byte > 0xff)) return false;
            if (produced >= output_size) return false;
            state->ring[position] = (uint8_t)byte;
            output[produced++] = (uint8_t)byte;
            position = (position + 1U) & (uint32_t)NID_RING_MASK;
        } else {
            int32_t high = 0;
            int32_t low;
            uint32_t source;
            int32_t i;

            if (length > 2) {
                high = nid_decode_symbol(&state->distance, &bits);
                if (high < 0) return false;
            }

            low = nid_read_bits(&bits, 8);
            if (low < 0) return false;

            /* Deliberate: a distance that reaches back before the start of the
             * block wraps into the still-zero tail of the 64 KiB ring rather
             * than failing.  The reference tolerates it and real members rely
             * on the zero fill, so the wrap is reproduced, not rejected. */
            source = (position - (uint32_t)(high * 0x100 + low)) & (uint32_t)NID_RING_MASK;

            for (i = 0; i < length; ++i) {
                uint8_t value;
                if (produced >= output_size) return false;
                value = state->ring[source];
                state->ring[position] = value;
                output[produced++] = value;
                source = (source + 1U) & (uint32_t)NID_RING_MASK;
                position = (position + 1U) & (uint32_t)NID_RING_MASK;
            }
        }
    }

    *out_position = produced;

    return true;
}

bool xx_nid_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written) {
    nid_state *state;
    size_t position = 0;
    size_t out_position = 0;
    bool result = true;

    if (written) *written = 0;
    if (!input && (input_size != 0)) return false;
    if (!output && (output_size != 0)) return false;
    if (output_size == 0) return true;

    state = (nid_state *)xx_mem_alloc(sizeof(nid_state));
    if (!state) return false;

    while (out_position < output_size) {
        uint32_t flags;
        size_t block_size;
        size_t body;

        if ((position > input_size) || ((input_size - position) < (size_t)NID_FRAME_SIZE)) {
            result = false;
            break;
        }

        /* input[position + 0] is the frame's unknown byte; never read. */
        flags = (uint32_t)input[position + 1] | ((uint32_t)input[position + 2] << 8);
        block_size = (size_t)input[position + 3] | ((size_t)input[position + 4] << 8);
        body = position + NID_FRAME_SIZE;

        if ((input_size - body) < block_size) {
            result = false;
            break;
        }

        if (flags & 0x800U) {
            if (!nid_decode_block(state, input + body, block_size, output, output_size, &out_position)) {
                result = false;
                break;
            }
        } else {
            /* The reference appends the stored block unconditionally and lets
             * the final exact-size test reject an overshoot; with a fixed
             * caller buffer the overshoot has to be refused up front, which
             * rejects exactly the same streams. */
            if (block_size > (output_size - out_position)) {
                result = false;
                break;
            }
            xx_rt_memcpy(output + out_position, input + body, block_size);
            out_position += block_size;
        }

        position = body + block_size;

        if (flags & 0x100U) break;
    }

    if (result) result = (out_position == output_size);

    xx_mem_free(state);

    if (result && written) *written = out_position;

    return result;
}
