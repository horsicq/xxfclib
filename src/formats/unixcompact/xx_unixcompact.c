/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Unix compact(1) -- the adaptive (FGK) Huffman companion of compress(1).
 * The container is about as small as a container gets:
 *
 *   +0  0xFF 0x1F   magic ("\377\037")
 *   +2  ...         adaptive Huffman code stream, terminated by the
 *                   in-band end-of-file symbol
 *
 * There is no stored plaintext length, no checksum and no tree image: the
 * coder starts from a fixed initial tree and adapts as it goes, which is why
 * files whose plaintexts share a prefix also share a coded prefix.  With no
 * length and no checksum the only statement the file makes about itself is
 * that it decodes: validation therefore runs a bounded trial decode rather
 * than trusting two bytes of magic, and correctness was established against
 * U3's own extraction of the corpus.  Do not confuse this with compress(1),
 * 0x1F 0x9D, which is handled by the unixcompress reader.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/unixcompact/xx_unixcompact.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef UNIX_COMPACT
#define XX_UNIXCOMPACT_FILE_TYPE XX_FILE_TYPE_UNIX_COMPACT
#else
#define XX_UNIXCOMPACT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_UNIXCOMPACT_PAYLOAD_NAME "payload"
#define XX_UNIXCOMPACT_MAGIC0 UINT8_C(0xff)
#define XX_UNIXCOMPACT_MAGIC1 UINT8_C(0x1f)
#define XX_UNIXCOMPACT_HEADER_SIZE 2
/* Two magic bytes plus at least one byte of code: anything shorter cannot
 * even carry the end-of-file symbol. */
#define XX_UNIXCOMPACT_MIN_SIZE 4

static void xx_unixcompact_vtable_destroy(Abstractformat *self);
/* Recognition needs the decoder; the decoder is below. */
static bool xx_unixcompact_trial_decode(Abstractformat *self,
                                        int64_t stream_offset,
                                        int64_t stream_size,
                                        xx_pd_struct *pd);

static bool xx_unixcompact_read_at(xx_io_device *device, int64_t offset,
                                   void *buffer, size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Validates the header and returns the extent of the coded stream.  Every
 * value handed back is already bounded by the real device size. */
static bool xx_unixcompact_scan(Abstractformat *self, int64_t *stream_offset,
                                int64_t *stream_size, xx_pd_struct *pd) {
    uint8_t magic[XX_UNIXCOMPACT_HEADER_SIZE];
    int64_t total_size;
    int64_t available;
    int64_t offset;
    int64_t size;
    if (!self || !self->device || self->base_address < 0 || !stream_offset ||
        !stream_size || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    available = total_size - self->base_address;
    if (available < (int64_t)XX_UNIXCOMPACT_MIN_SIZE) return false;
    if (!xx_unixcompact_read_at(self->device, self->base_address, magic,
                                sizeof(magic)) ||
        magic[0] != XX_UNIXCOMPACT_MAGIC0 ||
        magic[1] != XX_UNIXCOMPACT_MAGIC1) {
        return false;
    }
    /* compact(1) carries no length field, so the member runs to the end of
     * the supplied range. */
    offset = self->base_address + (int64_t)XX_UNIXCOMPACT_HEADER_SIZE;
    size = available - (int64_t)XX_UNIXCOMPACT_HEADER_SIZE;
    /* Two bytes of magic accept far too much, so recognition decodes a few
     * kilobytes before it believes the file -- which is what the reference
     * handler does here too. It also means the reader never claims a file it
     * then cannot extract. */
    if (!xx_unixcompact_trial_decode(self, offset, size, pd)) return false;
    *stream_offset = offset;
    *stream_size = size;
    return true;
}

/* ------------------------------------------------ adaptive Huffman ----- */

/*
 * compact(1)'s coder is Gallager's dynamic Huffman algorithm, the one Knuth
 * wrote up in "Dynamic Huffman Coding" and named this program as the source
 * of. Nothing about the tree is stored in the file: encoder and decoder build
 * the same tree from the same history, so the whole format is the initial
 * tree plus one update rule.
 *
 * The alphabet is 0..255 plus two controls: 0x100 ends the stream and 0x101
 * escapes a byte that has not been seen before, which is then sent raw in
 * eight bits and given a leaf of its own.
 *
 * The tree is held the way the original holds it, because the codes depend on
 * it exactly: one array of nodes, each node owning its two child SLOTS, and
 * the slots read in array order are in non-increasing weight order. That
 * ordering is the sibling property, and keeping it is the whole algorithm.
 * Slots of equal weight are grouped into "blocks", a singly linked list from
 * heaviest to lightest, each block naming the node that holds its first slot.
 * Incrementing a slot swaps its contents with its block's leader and then
 * repeats at the parent, which is what keeps the array sorted without ever
 * rebuilding the tree.
 *
 * The initial tree, for a first byte c that the file stores raw:
 *
 *            node0                 slot order:  node0.child0  weight 2
 *           /     \                             node0.child1  weight 1
 *       node1      c (1)                        node1.child0  weight 1
 *       /    \                                  node1.child1  weight 1
 *   ESC(1)  EOF(1)
 *
 * A new byte splits the LAST slot -- the lightest one -- into an internal
 * node holding the old occupant and the new byte at weight zero.
 *
 * This was recovered from the U3 handler: initial tree 0x006d8040, update
 * 0x006d7d00, slot relink 0x006d7c70, new symbol 0x006d7b70, decode loop
 * 0x006d8220.
 */

#define XX_UNIXCOMPACT_SYM_EOF 0x100
#define XX_UNIXCOMPACT_SYM_ESC 0x101
#define XX_UNIXCOMPACT_SYM_COUNT 0x102
/* Two nodes to start and one per byte value that is introduced after the
 * first, so 2 + 255. */
#define XX_UNIXCOMPACT_MAX_NODES 258
/* One block per slot in the worst case, plus the list head sentinel. */
#define XX_UNIXCOMPACT_MAX_BLOCKS (2 * XX_UNIXCOMPACT_MAX_NODES + 2)
/* Slot 0 of a node is a leaf. */
#define XX_UNIXCOMPACT_LEAF0 8U
/* Slot 1 of a node is a leaf. */
#define XX_UNIXCOMPACT_LEAF1 4U
/* This node is slot 1 of its parent. */
#define XX_UNIXCOMPACT_ISRIGHT 1U
/* A runaway guard: nothing in the file bounds the plaintext. */
#define XX_UNIXCOMPACT_MAX_OUTPUT ((size_t)256 * 1024 * 1024)
#define XX_UNIXCOMPACT_INITIAL_OUTPUT ((size_t)64 * 1024)

/* One branch of a node: the symbol or child node it leads to, the weight
 * behind it, and the block of equal-weight slots it belongs to. */
typedef struct xx_unixcompact_slot_s {
    int32_t value;
    int32_t block;
    int32_t count;
} xx_unixcompact_slot;

typedef struct xx_unixcompact_node_s {
    int32_t parent;
    uint32_t flags;
    xx_unixcompact_slot child[2];
} xx_unixcompact_node;

typedef struct xx_unixcompact_block_s {
    int32_t node; /* node holding this block's first slot */
    int32_t next; /* next lighter block, -1 at the end */
} xx_unixcompact_block;

typedef struct xx_unixcompact_tree_s {
    xx_unixcompact_node nodes[XX_UNIXCOMPACT_MAX_NODES];
    xx_unixcompact_block blocks[XX_UNIXCOMPACT_MAX_BLOCKS];
    int32_t block_free;
    int32_t last_node;
    /* Where each symbol's leaf lives: the node, and the slot index with bit 1
     * set to mark the entry live. */
    int32_t leaf_node[XX_UNIXCOMPACT_SYM_COUNT];
    int32_t leaf_info[XX_UNIXCOMPACT_SYM_COUNT];
} xx_unixcompact_tree;

typedef struct xx_unixcompact_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t cache;
    int available;
} xx_unixcompact_bits;

typedef struct xx_unixcompact_out_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
} xx_unixcompact_out;

static void xx_unixcompact_bits_init(xx_unixcompact_bits *bits,
                                     const uint8_t *data, size_t size) {
    bits->data = data;
    bits->size = size;
    bits->position = 0U;
    bits->cache = 0U;
    bits->available = 0;
}

/* Bits leave a byte from the top. Returns -1 once the input is exhausted;
 * there is no padding convention to fall back on. */
static int xx_unixcompact_read_bit(xx_unixcompact_bits *bits) {
    int result;

    if (bits->available == 0) {
        if (bits->position >= bits->size) return -1;
        bits->cache = bits->data[bits->position++];
        bits->available = 8;
    }
    result = (bits->cache >> 7) & 1;
    bits->cache = (uint8_t)(bits->cache << 1);
    --bits->available;
    return result;
}

static int xx_unixcompact_read_byte(xx_unixcompact_bits *bits) {
    int value = 0;
    int index;

    for (index = 0; index < 8; ++index) {
        int bit = xx_unixcompact_read_bit(bits);
        if (bit < 0) return -1;
        value = value * 2 + bit;
    }
    return value;
}

static void xx_unixcompact_tree_init(xx_unixcompact_tree *tree, int first) {
    int index;

    xx_mem_zero(tree, sizeof(*tree));
    for (index = 0; index < XX_UNIXCOMPACT_SYM_COUNT; ++index) {
        tree->leaf_node[index] = -1;
    }
    /* Block 0 is the list head sentinel, so that the heaviest slot always has
     * a predecessor to link through. */
    tree->blocks[0].node = -1;
    tree->blocks[0].next = 1;
    tree->blocks[1].node = 0; /* weight 2: node0.child0 */
    tree->blocks[1].next = 2;
    tree->blocks[2].node = 0; /* weight 1: node0.child1 onward */
    tree->blocks[2].next = -1;
    for (index = 3; index < XX_UNIXCOMPACT_MAX_BLOCKS; ++index) {
        tree->blocks[index].node = -1;
        tree->blocks[index].next =
            (index + 1 < XX_UNIXCOMPACT_MAX_BLOCKS) ? (index + 1) : -1;
    }
    tree->block_free = 3;

    tree->nodes[0].parent = -1;
    tree->nodes[0].flags = XX_UNIXCOMPACT_LEAF1;
    tree->nodes[0].child[0].value = 1;
    tree->nodes[0].child[0].block = 1;
    tree->nodes[0].child[0].count = 2;
    tree->nodes[0].child[1].value = first;
    tree->nodes[0].child[1].block = 2;
    tree->nodes[0].child[1].count = 1;

    tree->nodes[1].parent = 0;
    tree->nodes[1].flags = XX_UNIXCOMPACT_LEAF0 | XX_UNIXCOMPACT_LEAF1;
    tree->nodes[1].child[0].value = XX_UNIXCOMPACT_SYM_ESC;
    tree->nodes[1].child[0].block = 2;
    tree->nodes[1].child[0].count = 1;
    tree->nodes[1].child[1].value = XX_UNIXCOMPACT_SYM_EOF;
    tree->nodes[1].child[1].block = 2;
    tree->nodes[1].child[1].count = 1;

    tree->last_node = 1;
    tree->leaf_node[first] = 0;
    tree->leaf_info[first] = 3;
    tree->leaf_node[XX_UNIXCOMPACT_SYM_EOF] = 1;
    tree->leaf_info[XX_UNIXCOMPACT_SYM_EOF] = 3;
    tree->leaf_node[XX_UNIXCOMPACT_SYM_ESC] = 1;
    tree->leaf_info[XX_UNIXCOMPACT_SYM_ESC] = 2;
}

/* A slot's contents moved to (@p new_parent, @p new_index): tell whatever the
 * slot holds -- a child node or a symbol's leaf entry -- where it now lives.
 * @p node and @p child_index say which slot the value CAME FROM, because only
 * that slot's flag bit knows whether the value is a node or a symbol. */
static void xx_unixcompact_relink(xx_unixcompact_tree *tree, int node,
                                  int child_index, int value, int new_parent,
                                  uint32_t new_index) {
    const uint32_t mask = (child_index == 0) ? XX_UNIXCOMPACT_LEAF0
                                             : XX_UNIXCOMPACT_LEAF1;

    if ((mask & tree->nodes[node].flags) == 0U) {
        tree->nodes[value].parent = new_parent;
        tree->nodes[value].flags &= ~XX_UNIXCOMPACT_ISRIGHT;
        if (new_index != 0U) tree->nodes[value].flags |= new_index;
    } else {
        tree->leaf_node[value] = new_parent;
        tree->leaf_info[value] &= ~1;
        if (new_index != 0U) tree->leaf_info[value] |= (int)new_index;
    }
}

/* Add one to @p symbol's weight and to every weight above it, restoring the
 * slot ordering as it goes. */
static void xx_unixcompact_tree_update(xx_unixcompact_tree *tree,
                                       int symbol) {
    int node = tree->leaf_node[symbol];
    uint32_t index = (uint32_t)tree->leaf_info[symbol];

    if (node < 0) return;
    for (;;) {
        int count;
        int old_block;
        /* "above" is the slot before this one in the array, which is the
         * heavier neighbour; "below" is the lighter one after it. */
        int above_count = 0;
        int above_block = 0;
        int below_count = 0;
        int below_block = -1;
        int below_node = 0;

        index &= 1U;
        tree->nodes[node].child[index].count += 1;
        count = tree->nodes[node].child[index].count;
        old_block = tree->nodes[node].child[index].block;

        for (;;) {
            int leader;
            int leader_index;
            uint32_t is_right;
            int moved_in;
            int moved_out;
            uint32_t mask_here;
            uint32_t mask_there;

            if (index != 0U) {
                below_node = node + 1;
                if (node == tree->last_node) {
                    /* Nothing lighter exists; make the tests below behave as
                     * if the neighbour were two lighter. */
                    below_count = count - 2;
                    below_block = -1;
                } else {
                    below_count = tree->nodes[node + 1].child[0].count;
                    below_block = tree->nodes[node + 1].child[0].block;
                }
                above_count = tree->nodes[node].child[0].count;
                above_block = tree->nodes[node].child[0].block;
            } else {
                below_node = node;
                below_count = tree->nodes[node].child[1].count;
                below_block = tree->nodes[node].child[1].block;
                if (node != 0) {
                    above_count = tree->nodes[node - 1].child[1].count;
                    above_block = tree->nodes[node - 1].child[1].block;
                } else {
                    /* The first slot of all: nothing can be heavier. */
                    above_count = count + 1;
                    above_block = 0;
                }
            }

            if (count <= above_count) break;

            /* Swap with the leader of the block just above. The leader is
             * that block's first slot: slot 0 of its node unless slot 0 is
             * already too heavy to be in the block. */
            leader = tree->blocks[above_block].node;
            leader_index =
                (count <= tree->nodes[leader].child[0].count) ? 1 : 0;
            is_right = (uint32_t)leader_index;
            moved_in = tree->nodes[leader].child[leader_index].value;
            moved_out = tree->nodes[node].child[index].value;
            tree->nodes[leader].child[leader_index].value = moved_out;
            tree->nodes[node].child[index].value = moved_in;
            xx_unixcompact_relink(tree, leader, leader_index, moved_in, node,
                                  index);
            xx_unixcompact_relink(tree, node, (int)index, moved_out, leader,
                                  is_right);
            mask_here = (index == 0U) ? XX_UNIXCOMPACT_LEAF0
                                      : XX_UNIXCOMPACT_LEAF1;
            mask_there = (is_right == 0U) ? XX_UNIXCOMPACT_LEAF0
                                          : XX_UNIXCOMPACT_LEAF1;
            /* Both sides normalise to the same bit position, so this is just
             * "the two slots disagree about being a leaf". */
            if (((tree->nodes[node].flags & mask_here) << index) !=
                ((tree->nodes[leader].flags & mask_there) << is_right)) {
                tree->nodes[node].flags ^= mask_here;
                tree->nodes[leader].flags ^= mask_there;
            }
            /* The weights stay with the positions, not with the contents. */
            tree->nodes[leader].child[leader_index].count += 1;
            tree->nodes[node].child[index].count -= 1;
            node = leader;
            index = is_right;
            /* The block just lost its first slot to a heavier weight. Its
             * leader moves on, and only crosses into the next node when the
             * slot it lost was the node's second. */
            if (is_right != 0U) tree->blocks[above_block].node += 1;
        }

        if (count == above_count) {
            /* Joins the block above. */
            tree->nodes[node].child[index].block = above_block;
            if (below_count + 1 < count) {
                /* It was alone in its old block, which now disappears. */
                tree->blocks[above_block].next = below_block;
                tree->blocks[old_block].next = tree->block_free;
                tree->block_free = old_block;
            } else if (below_block >= 0) {
                tree->blocks[below_block].node = below_node;
            }
        } else if (count == below_count + 1) {
            /* It left the block it shared with the slot below and is not
             * heavy enough for the one above: it needs a block of its own. */
            int fresh = tree->block_free;
            if (fresh < 0) return;
            tree->block_free = tree->blocks[fresh].next;
            tree->blocks[fresh].next = below_block;
            tree->blocks[fresh].node = node;
            tree->blocks[above_block].next = fresh;
            if (below_block >= 0) tree->blocks[below_block].node = below_node;
            tree->nodes[node].child[index].block = fresh;
        }

        index = tree->nodes[node].flags;
        node = tree->nodes[node].parent;
        if (node < 0) return;
    }
}

/* Give @p symbol a leaf by splitting the lightest slot in the tree. */
static bool xx_unixcompact_tree_add(xx_unixcompact_tree *tree, int symbol) {
    int fresh = tree->block_free;
    int parent = tree->last_node;
    int added;
    int displaced;

    if (fresh < 0) return false;
    if (parent + 1 >= XX_UNIXCOMPACT_MAX_NODES) return false;
    /* The lightest slot must be a leaf; if it is not, the tree is not in a
     * state this format can produce. */
    if ((tree->nodes[parent].flags & XX_UNIXCOMPACT_LEAF1) == 0U) return false;
    displaced = tree->nodes[parent].child[1].value;
    if (displaced < 0 || displaced >= XX_UNIXCOMPACT_SYM_COUNT) return false;

    tree->block_free = tree->blocks[fresh].next;
    added = parent + 1;
    tree->last_node = added;
    tree->nodes[added].parent = parent;
    tree->leaf_info[symbol] = 3;

    tree->blocks[fresh].node = added;
    tree->nodes[parent].child[1].value = added;
    tree->leaf_node[displaced] = added;
    tree->leaf_node[symbol] = added;
    tree->nodes[added].flags = XX_UNIXCOMPACT_LEAF0 | XX_UNIXCOMPACT_LEAF1 |
                               XX_UNIXCOMPACT_ISRIGHT;
    tree->nodes[parent].flags &= ~XX_UNIXCOMPACT_LEAF1;
    tree->leaf_info[displaced] = 2;
    /* The displaced leaf keeps its weight and block; the new one starts at
     * zero in a block of its own, appended below. */
    tree->nodes[added].child[0].value = displaced;
    tree->nodes[added].child[0].count = tree->nodes[parent].child[1].count;
    tree->nodes[added].child[0].block = tree->nodes[parent].child[1].block;
    tree->nodes[added].child[1].value = symbol;
    tree->nodes[added].child[1].count = 0;
    tree->blocks[tree->nodes[parent].child[1].block].next = fresh;
    tree->nodes[added].child[1].block = fresh;
    tree->blocks[fresh].next = -1;
    return true;
}

static bool xx_unixcompact_out_reserve(xx_unixcompact_out *out, size_t extra) {
    size_t wanted;
    size_t capacity;
    uint8_t *grown;

    if (extra > XX_UNIXCOMPACT_MAX_OUTPUT - out->size) return false;
    wanted = out->size + extra;
    if (wanted <= out->capacity) return true;
    capacity = out->capacity ? out->capacity : XX_UNIXCOMPACT_INITIAL_OUTPUT;
    while (capacity < wanted) {
        if (capacity > XX_UNIXCOMPACT_MAX_OUTPUT / 2U) {
            capacity = XX_UNIXCOMPACT_MAX_OUTPUT;
            break;
        }
        capacity *= 2U;
    }
    if (capacity < wanted) return false;
    grown = (uint8_t *)xx_mem_realloc(out->data, capacity);
    if (!grown) return false;
    out->data = grown;
    out->capacity = capacity;
    return true;
}

/*
 * Decode the coded stream. @p limit caps how much plaintext is produced --
 * pass 0 for "no cap" -- so that validation can run a short trial decode
 * without materialising a whole file.
 *
 * Returns true only on a clean end-of-file symbol, or on hitting @p limit,
 * which is what tells a real stream from two bytes that happened to be 0xFF
 * 0x1F. @p complete says which of the two it was.
 */
static bool xx_unixcompact_huff_decode(const uint8_t *input, size_t input_size,
                                       size_t limit,
                                       xx_unixcompact_out *out,
                                       bool *complete,
                                       xx_unixcompact_bits *tail,
                                       xx_pd_struct *pd) {
    xx_unixcompact_tree *tree;
    xx_unixcompact_bits bits;
    uint8_t seen[256];
    int node;
    bool result = false;

    out->data = NULL;
    out->size = 0U;
    out->capacity = 0U;
    if (complete) *complete = false;
    if (!input || input_size < 1U) return false;

    tree = (xx_unixcompact_tree *)xx_mem_alloc(sizeof(*tree));
    if (!tree) return false;
    xx_mem_zero(seen, sizeof(seen));

    /* The first byte of plaintext is stored raw: it is what the initial tree
     * is built around. */
    xx_unixcompact_bits_init(&bits, input, input_size);
    {
        int first = input[0];
        bits.position = 1U;
        seen[first] = 1U;
        xx_unixcompact_tree_init(tree, first);
        if (!xx_unixcompact_out_reserve(out, 1U)) goto done;
        out->data[out->size++] = (uint8_t)first;
    }

    node = 0;
    for (;;) {
        int bit;
        uint32_t mask;
        int symbol;

        if (limit != 0U && out->size >= limit) {
            result = true;
            goto done;
        }
        if (pd && xx_pd_is_stopped(pd)) goto done;
        bit = xx_unixcompact_read_bit(&bits);
        if (bit < 0) goto done;
        mask = (bit == 0) ? XX_UNIXCOMPACT_LEAF0 : XX_UNIXCOMPACT_LEAF1;
        if ((mask & tree->nodes[node].flags) == 0U) {
            node = tree->nodes[node].child[bit].value;
            if (node < 0 || node >= XX_UNIXCOMPACT_MAX_NODES) goto done;
            continue;
        }
        symbol = tree->nodes[node].child[bit].value;
        if (symbol == XX_UNIXCOMPACT_SYM_EOF) {
            if (complete) *complete = true;
            result = true;
            goto done;
        }
        if (symbol == XX_UNIXCOMPACT_SYM_ESC) {
            /* The escape is a symbol like any other and is counted before the
             * byte it introduces. */
            xx_unixcompact_tree_update(tree, XX_UNIXCOMPACT_SYM_ESC);
            symbol = xx_unixcompact_read_byte(&bits);
            if (symbol < 0) goto done;
            /* A byte introduced twice means this is not a compact stream. */
            if (seen[symbol]) goto done;
            seen[symbol] = 1U;
            if (!xx_unixcompact_tree_add(tree, symbol)) goto done;
        } else if (symbol < 0 || symbol > 0xFF) {
            goto done;
        }
        xx_unixcompact_tree_update(tree, symbol);
        if (!xx_unixcompact_out_reserve(out, 1U)) goto done;
        out->data[out->size++] = (uint8_t)symbol;
        node = 0;
    }

done:
    if (tail) *tail = bits;
    xx_mem_free(tree);
    if (!result) {
        xx_mem_free(out->data);
        out->data = NULL;
        out->size = 0U;
        out->capacity = 0U;
    }
    return result;
}

/*
 * How much of a candidate is decoded before it is believed, and how much of
 * it is read to do that.
 */
#define XX_UNIXCOMPACT_TRIAL_INPUT ((size_t)64 * 1024)
#define XX_UNIXCOMPACT_TRIAL_OUTPUT ((size_t)4 * 1024)

static bool xx_unixcompact_trial_decode(Abstractformat *self,
                                        int64_t stream_offset,
                                        int64_t stream_size,
                                        xx_pd_struct *pd) {
    xx_unixcompact_out out;
    uint8_t *packed;
    size_t wanted;
    bool complete = false;
    bool result;

    if (!self || !self->device || stream_offset < 0 || stream_size < 1) {
        return false;
    }
    wanted = (stream_size > (int64_t)XX_UNIXCOMPACT_TRIAL_INPUT)
                 ? XX_UNIXCOMPACT_TRIAL_INPUT
                 : (size_t)stream_size;
    packed = (uint8_t *)xx_mem_alloc(wanted);
    if (!packed) return false;
    if (!xx_unixcompact_read_at(self->device, stream_offset, packed, wanted)) {
        xx_mem_free(packed);
        return false;
    }
    /* Either the stream ended inside the prefix or it produced the whole
     * trial quota without going wrong. Running out of bits partway through
     * is the failure this is here to catch. */
    result = xx_unixcompact_huff_decode(packed, wanted,
                                        XX_UNIXCOMPACT_TRIAL_OUTPUT, &out,
                                        &complete, NULL, pd);
    xx_mem_free(out.data);
    xx_mem_free(packed);
    return result;
}

static bool xx_unixcompact_copy_options(xx_list_s *destination,
                                        const xx_list_s *source) {
    size_t index;
    if (!destination) return false;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_unixcompact_get_option(const xx_list_s *options,
                                               uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

static bool xx_unixcompact_populate_record(Abstractformat *self,
                                           xx_archive_record *record) {
    const xx_unixcompact *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    archive = (const xx_unixcompact *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = XX_UNIXCOMPACT_HEADER_SIZE;
    record->data_offset = archive->stream_offset;
    record->compressed_size = archive->stream_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_UNIXCOMPACT_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)archive->stream_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_unixcompact_init(xx_unixcompact *archive, xx_io_device *device,
                         int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_UNIXCOMPACT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-compact");
    xx_format_set_extension(&archive->format, "C");
    archive->format.check_is_valid = xx_unixcompact_check_is_valid;
    archive->format.handle_base_info = xx_unixcompact_handle_base_info;
    archive->format.get_format_size = xx_unixcompact_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_unixcompact_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_unixcompact_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_unixcompact_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_unixcompact_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_unixcompact_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_unixcompact_free_archive_records_reading;
    archive->format.destroy = xx_unixcompact_vtable_destroy;
    archive->stream_offset = -1;
    archive->stream_size = -1;
}

xx_unixcompact *xx_unixcompact_create(xx_io_device *device,
                                      int64_t base_address) {
    xx_unixcompact *archive =
        (xx_unixcompact *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_unixcompact_init(archive, device, base_address);
    return archive;
}

void xx_unixcompact_destroy(xx_unixcompact *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->stream_offset = -1;
    archive->stream_size = -1;
}

static void xx_unixcompact_vtable_destroy(Abstractformat *self) {
    xx_unixcompact_destroy((xx_unixcompact *)self);
}

void xx_unixcompact_free(xx_unixcompact *archive) {
    if (!archive) return;
    xx_unixcompact_destroy(archive);
    xx_mem_free(archive);
}

bool xx_unixcompact_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    int64_t stream_offset;
    int64_t stream_size;
    return xx_unixcompact_scan(self, &stream_offset, &stream_size, pd);
}

bool xx_unixcompact_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_unixcompact *archive;
    int64_t stream_offset;
    int64_t stream_size;
    if (!self) return false;
    archive = (xx_unixcompact *)self;
    if (!xx_unixcompact_scan(self, &stream_offset, &stream_size, pd)) {
        archive->stream_offset = -1;
        archive->stream_size = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    archive->stream_offset = stream_offset;
    archive->stream_size = stream_size;
    self->format_size = (int64_t)XX_UNIXCOMPACT_HEADER_SIZE + stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_UNIXCOMPACT_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_unixcompact_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_unixcompact_get_number_of_archive_records(Abstractformat *self,
                                                      xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

xx_archive_record_state *xx_unixcompact_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_unixcompact_copy_options(&state->options, options) ||
        !xx_unixcompact_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_unixcompact_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_unixcompact_archive_record_move_to_next(Abstractformat *self,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

/* Read the packed extent the reader already computed and decode all of it.
 * A stream that stops without its end-of-file symbol is refused: with no
 * length and no checksum, reaching that symbol is the only evidence the
 * plaintext is whole. */
static bool xx_unixcompact_decode(Abstractformat *self, uint8_t **plain,
                                  size_t *plain_size, xx_pd_struct *pd) {
    const xx_unixcompact *archive;
    xx_unixcompact_out out;
    uint8_t *packed;
    bool complete = false;

    *plain = NULL;
    *plain_size = 0U;
    if (!self || !self->is_valid || !self->base_info_handled) return false;
    archive = (const xx_unixcompact *)self;
    if (archive->stream_offset < 0 || archive->stream_size < 1 ||
        archive->stream_size > (int64_t)XX_UNIXCOMPACT_MAX_OUTPUT) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)archive->stream_size);
    if (!packed) return false;
    if (!xx_unixcompact_read_at(self->device, archive->stream_offset, packed,
                                (size_t)archive->stream_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_unixcompact_huff_decode(packed, (size_t)archive->stream_size, 0U,
                                    &out, &complete, NULL, pd) ||
        !complete) {
        xx_mem_free(out.data);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *plain = out.data;
    *plain_size = out.size;
    return true;
}

bool xx_unixcompact_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }

    path_option = xx_unixcompact_get_option(&state->options,
                                            XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the stream
         * without writing anything. */
        result = xx_unixcompact_decode(self, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path =
            xx_str_concat3(base_path, "/", XX_UNIXCOMPACT_PAYLOAD_NAME);
    } else {
        target_path = xx_str_concat(base_path, XX_UNIXCOMPACT_PAYLOAD_NAME);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_unixcompact_decode(self, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_unixcompact_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

int64_t xx_unixcompact_get_stream_offset(const xx_unixcompact *archive) {
    return archive ? archive->stream_offset : -1;
}

int64_t xx_unixcompact_get_stream_size(const xx_unixcompact *archive) {
    return archive ? archive->stream_size : -1;
}
