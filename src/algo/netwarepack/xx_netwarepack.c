/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Novell NetWare "Packed File" (version 0x01 / method 0x0A) decoder.
 * Ported from XArchive/Algos/xnetwarepackdecoder.cpp, token for token.
 */
#include "xxfclib/algo/netwarepack/xx_netwarepack.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define NWP_WINDOW_SIZE 0x4000
#define NWP_WINDOW_MASK 0x3FFF
#define NWP_MAX_TABLE_ENTRIES 256
#define NWP_LENGTH_ESCAPE 0xFE
#define NWP_ESCAPE_LENGTH_BITS 13
#define NWP_DISTANCE_LOW_BITS 5

/* The pre-order table holds at most 256 nodes, so the binary decoding tree
 * built from it holds at most 2 * 256 - 1 of them.  Three trees share one
 * pool. */
#define NWP_MAX_NODES ((2 * NWP_MAX_TABLE_ENTRIES) - 1)
#define NWP_POOL_SIZE (3 * NWP_MAX_NODES)
#define NWP_TREE_COUNT 3
/* A code cannot be longer than the number of nodes on the path to a leaf. */
#define NWP_MAX_CODE_BITS NWP_MAX_NODES

/* LSB-first bit reader.  Bytes are shifted in above whatever is already
 * buffered and fields are taken from the bottom, so the first bit read is the
 * least significant bit of the value. */
typedef struct nwp_bits {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t accumulator;
    unsigned count;
} nwp_bits;

typedef struct nwp_entry {
    uint8_t symbol;
    uint8_t depth;
} nwp_entry;

typedef struct nwp_node {
    int32_t left;
    int32_t right;
    uint8_t symbol;
    uint8_t leaf;
} nwp_node;

/* All mutable state lives here and the caller owns the allocation: no module
 * level scratch, so the decoder is re-entrant and thread safe. */
typedef struct nwp_state {
    nwp_node nodes[NWP_POOL_SIZE];
    int32_t node_count;
    int32_t root[NWP_TREE_COUNT];
    uint8_t window[NWP_WINDOW_SIZE];
} nwp_state;

static bool nwp_read(nwp_bits *reader, unsigned bits, uint32_t *value)
{
    /* bits is never more than 13 here, so the accumulator cannot overflow. */
    while (reader->count < bits) {
        if (reader->position >= reader->size) return false;
        reader->accumulator |=
            (uint32_t)reader->data[reader->position] << reader->count;
        reader->position++;
        reader->count += 8U;
    }

    *value = reader->accumulator & (((uint32_t)1 << bits) - 1U);
    reader->accumulator >>= bits;
    reader->count -= bits;

    return true;
}

static int32_t nwp_allocate(nwp_state *state)
{
    int32_t index;

    if (state->node_count >= NWP_POOL_SIZE) return -1;
    index = state->node_count;
    state->node_count++;
    state->nodes[index].left = -1;
    state->nodes[index].right = -1;
    state->nodes[index].symbol = 0;
    state->nodes[index].leaf = 0;

    return index;
}

/* Pre-order description reader.  `count` siblings sit at `depth`; each one is
 * a unary child count (k zero bits then a 1), an 8-bit symbol, and then its
 * own children one level down. */
static bool nwp_read_table(nwp_bits *reader, nwp_entry *table,
                           int32_t *total, int32_t count, int32_t depth)
{
    int32_t i;

    /* The 256-entry ceiling bounds the recursion, but a depth that no longer
     * fits the stored byte could never be matched by the builder anyway. */
    if ((count < 0) || (depth > 0xFF)) return false;

    for (i = 0; i < count; i++) {
        int32_t children = 0;
        uint32_t symbol = 0;

        for (;;) {
            uint32_t bit = 0;
            if (!nwp_read(reader, 1U, &bit)) return false;
            if (bit) break;
            children++;
            /* No node can have more children than the table can hold. */
            if (children > NWP_MAX_TABLE_ENTRIES) return false;
        }

        if (!nwp_read(reader, 8U, &symbol)) return false;

        if (*total >= NWP_MAX_TABLE_ENTRIES) return false;
        table[*total].symbol = (uint8_t)symbol;
        table[*total].depth = (uint8_t)depth;
        (*total)++;

        if (!nwp_read_table(reader, table, total, children, depth + 1))
            return false;
    }

    return true;
}

/* Turns the pre-order (symbol, depth) list into the binary decoding tree.  A
 * range is split at the LAST entry whose depth equals the current level: that
 * entry and everything after it become the 1 branch one level deeper, and
 * everything before it stays on the 0 branch at the same level.  A single
 * remaining entry is the leaf that carries its symbol. */
static bool nwp_build_tree(nwp_state *state, int32_t node,
                           const nwp_entry *table, int32_t start,
                           int32_t count, int32_t depth)
{
    int32_t i;

    if ((node < 0) || (count <= 0)) return false;

    if (count == 1) {
        state->nodes[node].leaf = 1;
        state->nodes[node].symbol = table[start].symbol;
        return true;
    }

    /* Depths past a byte cannot appear in the table, so a range that deep is
     * a malformed description rather than an unreachable branch. */
    if (depth > 0xFF) return false;

    for (i = (start + count) - 1; i >= start; i--) {
        int32_t left;
        int32_t right;

        if ((int32_t)table[i].depth != depth) continue;

        /* A split that leaves nothing on the 0 branch would build a node with
         * no children at all; refuse it instead of publishing a tree that
         * dies only if that branch happens to be taken. */
        if (i <= start) return false;

        left = nwp_allocate(state);
        right = nwp_allocate(state);
        if ((left < 0) || (right < 0)) return false;

        state->nodes[node].left = left;
        state->nodes[node].right = right;

        if (!nwp_build_tree(state, left, table, start, i - start, depth))
            return false;

        return nwp_build_tree(state, right, table, i, (start + count) - i,
                              depth + 1);
    }

    return false;
}

static bool nwp_read_tree(nwp_bits *reader, nwp_state *state,
                          int32_t tree_index)
{
    nwp_entry table[NWP_MAX_TABLE_ENTRIES];
    int32_t count = 0;
    int32_t root;

    /* One root node at depth 0; everything else hangs off it. */
    if (!nwp_read_table(reader, table, &count, 1, 0)) return false;
    if (count <= 0) return false;

    /* Deliberately NOT checked here: whether a symbol appears twice.  The
     * reference enforces uniqueness only on its detection probe, never on the
     * decode path, so that a stream the original tool would read is not lost
     * to a rule the original tool never had.  Do not "fix" this. */

    root = nwp_allocate(state);
    if (root < 0) return false;
    state->root[tree_index] = root;

    /* Deliberate: the builder starts one level BELOW the root entry's own
     * stored depth (0), i.e. at depth 1.  This is load-bearing - the split
     * rule above is relative to that level. */
    return nwp_build_tree(state, root, table, 0, count, 1);
}

static bool nwp_decode_symbol(nwp_bits *reader, const nwp_state *state,
                              int32_t tree_index, uint32_t *symbol)
{
    int32_t node = state->root[tree_index];
    int32_t step;

    for (step = 0; step <= NWP_MAX_CODE_BITS; step++) {
        uint32_t bit = 0;

        if (node < 0) return false;

        if (state->nodes[node].leaf) {
            *symbol = state->nodes[node].symbol;
            return true;
        }

        if (!nwp_read(reader, 1U, &bit)) return false;

        node = bit ? state->nodes[node].right : state->nodes[node].left;
    }

    return false;
}

bool xx_netwarepack_decode_memory(const uint8_t *input, size_t input_size,
                                  uint8_t *output, size_t output_size,
                                  size_t *written)
{
    nwp_bits reader;
    nwp_state *state;
    size_t produced = 0;
    size_t remaining;
    int32_t position = 0;
    int32_t i;
    bool result = true;

    if (written) *written = 0;
    if (!input || (input_size == 0)) return false;
    if (!output && (output_size != 0)) return false;

    /* Note there is deliberately no early exit for output_size == 0: even a
     * zero-length member has to carry three parsable trees, and returning
     * success before reading them would report OK on bytes never looked at. */

    state = (nwp_state *)xx_mem_alloc(sizeof(nwp_state));
    if (!state) return false;

    state->node_count = 0;
    for (i = 0; i < NWP_TREE_COUNT; i++) state->root[i] = -1;
    /* Zero initialisation is observable: a match may legally reach behind the
     * start of output and must then produce NUL bytes rather than fail. */
    xx_rt_memset(state->window, 0, sizeof(state->window));

    reader.data = input;
    reader.size = input_size;
    reader.position = 0;
    reader.accumulator = 0;
    reader.count = 0;

    for (i = 0; result && (i < NWP_TREE_COUNT); i++) {
        result = nwp_read_tree(&reader, state, i);
    }

    remaining = output_size;

    /* Every iteration consumes at least the flag bit, so the bounded input is
     * what terminates this loop; no separate iteration budget is needed. */
    while (result && (remaining > 0)) {
        uint32_t flag = 0;

        if (!nwp_read(&reader, 1U, &flag)) {
            result = false;
            break;
        }

        if (flag) {
            uint32_t symbol = 0;

            if (!nwp_decode_symbol(&reader, state, 0, &symbol)) {
                result = false;
                break;
            }

            state->window[position] = (uint8_t)symbol;
            position = (position + 1) & NWP_WINDOW_MASK;
            output[produced] = (uint8_t)symbol;
            produced++;
            remaining--;
        } else {
            uint32_t length = 0;
            uint32_t low = 0;
            uint32_t high = 0;
            uint32_t distance;
            int32_t source;
            size_t copy;
            size_t n;

            if (!nwp_decode_symbol(&reader, state, 1, &length)) {
                result = false;
                break;
            }
            if (length == NWP_LENGTH_ESCAPE) {
                if (!nwp_read(&reader, NWP_ESCAPE_LENGTH_BITS, &length)) {
                    result = false;
                    break;
                }
            }

            if (!nwp_read(&reader, NWP_DISTANCE_LOW_BITS, &low)) {
                result = false;
                break;
            }

            if (!nwp_decode_symbol(&reader, state, 2, &high)) {
                result = false;
                break;
            }

            /* A zero-length token would make no progress at all; no stream in
             * the reference corpus emits one, and accepting it here would
             * only spin until the input ran out. */
            if (length == 0) {
                result = false;
                break;
            }

            distance = low + (high * 32U);

            /* Distance 0 aims the copy source at the write pointer itself, so
             * every byte "copied" is the stale window cell it is about to
             * overwrite: the walk still terminates and still produces exactly
             * the declared length, but the bytes are invented. */
            if (distance == 0) {
                result = false;
                break;
            }

            source = (int32_t)(((uint32_t)position - distance) &
                               NWP_WINDOW_MASK);

            /* Deliberate, and matched from the reference: a match that would
             * run past the declared size is CLAMPED, not rejected.  The
             * declared size is the only end-of-stream signal the format has.
             * This never truncates the result - `remaining` still reaches 0,
             * so the full declared output is produced. */
            copy = (size_t)length;
            if (copy > remaining) copy = remaining;

            for (n = 0; n < copy; n++) {
                const uint8_t byte = state->window[source];
                state->window[position] = byte;
                position = (position + 1) & NWP_WINDOW_MASK;
                source = (source + 1) & NWP_WINDOW_MASK;
                output[produced] = byte;
                produced++;
                remaining--;
            }
        }
    }

    if (result && (remaining != 0)) result = false;

    xx_mem_free(state);

    /* The declared size is the only end-of-stream signal there is; publishing
     * a buffer that does not match it exactly would be publishing a guess. */
    if (result && (produced != output_size)) result = false;

    if (!result) return false;

    if (written) *written = produced;

    return true;
}
