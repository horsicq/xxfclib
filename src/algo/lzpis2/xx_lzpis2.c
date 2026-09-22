/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LZPIS2 is a sequence of separately compressed installer payload chunks.
 * Each chunk starts with a fresh 8 KiB LZSS window and a 320-symbol adaptive
 * Huffman tree.  Bits and the position prefix code are most-significant-bit
 * first.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzpis2/xx_lzpis2.h"

#include <string.h>

#define XX_LZPIS2_MAGIC_SIZE 6U
#define XX_LZPIS2_CHUNK_HEADER_SIZE 4U
#define XX_LZPIS2_MAX_CHUNK_OUTPUT 0x1000U
#define XX_LZPIS2_MAX_INPUT ((size_t)0x10000000U)
#define XX_LZPIS2_MAX_OUTPUT UINT64_C(0x40000000)
#define XX_LZPIS2_MAX_CHUNKS UINT32_C(0x100000)

#define XX_LZPIS2_WINDOW_SIZE 8192U
#define XX_LZPIS2_MAX_MATCH 66U
#define XX_LZPIS2_MATCH_THRESHOLD 2U
#define XX_LZPIS2_SYMBOL_COUNT 320U
#define XX_LZPIS2_TREE_NODE_COUNT (XX_LZPIS2_SYMBOL_COUNT * 2U - 1U)
#define XX_LZPIS2_TREE_ROOT (XX_LZPIS2_TREE_NODE_COUNT - 1U)
#define XX_LZPIS2_REBUILD_FREQUENCY UINT16_C(0x8000)

typedef struct xx_lzpis2_bits_s {
    const uint8_t *data;
    size_t size;
    size_t byte_position;
    uint32_t pending;
    unsigned pending_bits;
    uint64_t used_bits;
} xx_lzpis2_bits;

typedef struct xx_lzpis2_huffman_s {
    uint16_t frequency[XX_LZPIS2_TREE_NODE_COUNT + 1U];
    uint16_t child[XX_LZPIS2_TREE_NODE_COUNT];
    uint16_t parent[XX_LZPIS2_TREE_NODE_COUNT + XX_LZPIS2_SYMBOL_COUNT];
} xx_lzpis2_huffman;

static uint16_t xx_lzpis2_read16le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

/*
 * The encoder flushes whole bytes, so the final token of a chunk may legally
 * extend into the byte padding past the declared compressed size.  Reads past
 * the end therefore yield zero bits rather than failing outright; overrun is
 * caught afterwards by xx_lzpis2_bits_overrun(), which compares the bits
 * actually consumed against the chunk's declared length.  Failing here instead
 * rejected every chunk whose last token straddled the padding.
 */
static bool xx_lzpis2_get_bits(xx_lzpis2_bits *bits, unsigned count,
                                uint32_t *value) {
    uint32_t mask;
    if (!bits || !value || count == 0U || count > 16U) return false;
    while (bits->pending_bits < count) {
        uint8_t next = bits->byte_position < bits->size
                           ? bits->data[bits->byte_position]
                           : 0U;
        if (bits->byte_position < bits->size) ++bits->byte_position;
        bits->pending = (bits->pending << 8U) | (uint32_t)next;
        bits->pending_bits += 8U;
    }
    mask = (UINT32_C(1) << count) - 1U;
    bits->pending_bits -= count;
    bits->used_bits += (uint64_t)count;
    *value = (bits->pending >> bits->pending_bits) & mask;
    return true;
}

/* True once the decoder has consumed more whole bytes than the chunk declares,
 * which means the stream was not a valid chunk after all. */
static bool xx_lzpis2_bits_overrun(const xx_lzpis2_bits *bits) {
    if (!bits) return true;
    return (bits->used_bits + 7U) / 8U > (uint64_t)bits->size;
}

/*
 * The position tree may inspect up to nine bits even when the next code ends
 * earlier.  Treat unavailable look-ahead as zero, but consume only the
 * selected prefix with xx_lzpis2_get_bits(), which remains strictly bounded.
 */
static uint32_t xx_lzpis2_peek_zero_padded(const xx_lzpis2_bits *bits,
                                            unsigned count) {
    uint32_t accumulator;
    unsigned available;
    size_t position;
    uint32_t mask;
    if (!bits || count == 0U || count > 16U) return 0U;
    accumulator = bits->pending;
    available = bits->pending_bits;
    position = bits->byte_position;
    while (available < count) {
        uint8_t next = position < bits->size ? bits->data[position] : 0U;
        if (position < bits->size) ++position;
        accumulator = (accumulator << 8U) | (uint32_t)next;
        available += 8U;
    }
    mask = (UINT32_C(1) << count) - 1U;
    return (accumulator >> (available - count)) & mask;
}

static void xx_lzpis2_huffman_init(xx_lzpis2_huffman *tree) {
    unsigned leaf;
    unsigned node;
    if (!tree) return;
    xx_rt_memset(tree, 0, sizeof(*tree));
    for (leaf = 0U; leaf < XX_LZPIS2_SYMBOL_COUNT; ++leaf) {
        tree->frequency[leaf] = 1U;
        tree->child[leaf] =
            (uint16_t)(leaf + XX_LZPIS2_TREE_NODE_COUNT);
        tree->parent[leaf + XX_LZPIS2_TREE_NODE_COUNT] = (uint16_t)leaf;
    }
    leaf = 0U;
    for (node = XX_LZPIS2_SYMBOL_COUNT; node <= XX_LZPIS2_TREE_ROOT;
         ++node) {
        tree->frequency[node] =
            (uint16_t)(tree->frequency[leaf] + tree->frequency[leaf + 1U]);
        tree->child[node] = (uint16_t)leaf;
        tree->parent[leaf] = (uint16_t)node;
        tree->parent[leaf + 1U] = (uint16_t)node;
        leaf += 2U;
    }
    tree->frequency[XX_LZPIS2_TREE_NODE_COUNT] = UINT16_MAX;
    tree->parent[XX_LZPIS2_TREE_ROOT] = 0U;
}

static bool xx_lzpis2_huffman_rebuild(xx_lzpis2_huffman *tree) {
    unsigned source;
    unsigned destination = 0U;
    unsigned left;
    unsigned node;
    if (!tree) return false;

    for (source = 0U; source < XX_LZPIS2_TREE_NODE_COUNT; ++source) {
        if (tree->child[source] >= XX_LZPIS2_TREE_NODE_COUNT) {
            if (destination >= XX_LZPIS2_SYMBOL_COUNT) return false;
            tree->frequency[destination] =
                (uint16_t)((tree->frequency[source] + 1U) / 2U);
            tree->child[destination] = tree->child[source];
            ++destination;
        }
    }
    if (destination != XX_LZPIS2_SYMBOL_COUNT) return false;

    left = 0U;
    for (node = XX_LZPIS2_SYMBOL_COUNT; node < XX_LZPIS2_TREE_NODE_COUNT;
         ++node) {
        uint16_t sum =
            (uint16_t)(tree->frequency[left] + tree->frequency[left + 1U]);
        unsigned insert = node;
        unsigned move;
        while (insert > 0U && sum < tree->frequency[insert - 1U]) {
            --insert;
        }
        for (move = node; move > insert; --move) {
            tree->frequency[move] = tree->frequency[move - 1U];
            tree->child[move] = tree->child[move - 1U];
        }
        tree->frequency[insert] = sum;
        tree->child[insert] = (uint16_t)left;
        left += 2U;
    }

    for (node = 0U; node < XX_LZPIS2_TREE_NODE_COUNT; ++node) {
        uint16_t first = tree->child[node];
        if (first >=
            XX_LZPIS2_TREE_NODE_COUNT + XX_LZPIS2_SYMBOL_COUNT) {
            return false;
        }
        tree->parent[first] = (uint16_t)node;
        if (first < XX_LZPIS2_TREE_NODE_COUNT) {
            tree->parent[first + 1U] = (uint16_t)node;
        }
    }
    return true;
}

static bool xx_lzpis2_huffman_update(xx_lzpis2_huffman *tree,
                                      unsigned symbol) {
    unsigned node;
    unsigned steps = 0U;
    if (!tree || symbol >= XX_LZPIS2_SYMBOL_COUNT) return false;
    if (tree->frequency[XX_LZPIS2_TREE_ROOT] ==
        XX_LZPIS2_REBUILD_FREQUENCY &&
        !xx_lzpis2_huffman_rebuild(tree)) {
        return false;
    }
    /* Walk from the symbol's leaf slot up to the root, incrementing each
     * frequency and restoring the sibling ordering.  This must be a do/while:
     * parent[symbol + TREE_NODE_COUNT] is initialised to `symbol`, so for
     * symbol 0 the starting node is legitimately 0 and a leading `while`
     * test skipped the update entirely, desynchronising the adaptive tree
     * from the encoder on the first occurrence of symbol 0. */
    node = tree->parent[symbol + XX_LZPIS2_TREE_NODE_COUNT];
    do {
        uint16_t updated;
        unsigned candidate;
        if (node >= XX_LZPIS2_TREE_NODE_COUNT ||
            ++steps > XX_LZPIS2_TREE_NODE_COUNT) {
            return false;
        }
        updated = (uint16_t)(tree->frequency[node] + 1U);
        tree->frequency[node] = updated;
        candidate = node + 1U;
        if (candidate > XX_LZPIS2_TREE_NODE_COUNT) return false;
        if (updated > tree->frequency[candidate]) {
            uint16_t moved_child;
            uint16_t displaced_child;
            while (candidate < XX_LZPIS2_TREE_NODE_COUNT &&
                   updated > tree->frequency[candidate]) {
                ++candidate;
            }
            --candidate;
            moved_child = tree->child[node];
            displaced_child = tree->child[candidate];
            tree->frequency[node] = tree->frequency[candidate];
            tree->frequency[candidate] = updated;
            tree->child[node] = displaced_child;
            tree->child[candidate] = moved_child;
            if (moved_child >= XX_LZPIS2_TREE_NODE_COUNT +
                                  XX_LZPIS2_SYMBOL_COUNT ||
                displaced_child >= XX_LZPIS2_TREE_NODE_COUNT +
                                       XX_LZPIS2_SYMBOL_COUNT) {
                return false;
            }
            tree->parent[moved_child] = (uint16_t)candidate;
            if (moved_child < XX_LZPIS2_TREE_NODE_COUNT) {
                tree->parent[moved_child + 1U] = (uint16_t)candidate;
            }
            tree->parent[displaced_child] = (uint16_t)node;
            if (displaced_child < XX_LZPIS2_TREE_NODE_COUNT) {
                tree->parent[displaced_child + 1U] = (uint16_t)node;
            }
            node = candidate;
        }
        node = tree->parent[node];
    } while (node != 0U);
    return true;
}

static bool xx_lzpis2_huffman_decode_symbol(xx_lzpis2_huffman *tree,
                                             xx_lzpis2_bits *bits,
                                             unsigned *symbol) {
    uint16_t node;
    unsigned guard = 0U;
    if (!tree || !bits || !symbol) return false;
    node = tree->child[XX_LZPIS2_TREE_ROOT];
    while (node < XX_LZPIS2_TREE_NODE_COUNT) {
        uint32_t bit;
        if (++guard > XX_LZPIS2_TREE_NODE_COUNT ||
            !xx_lzpis2_get_bits(bits, 1U, &bit)) {
            return false;
        }
        node = (uint16_t)(node + bit);
        if (node >= XX_LZPIS2_TREE_NODE_COUNT) return false;
        node = tree->child[node];
    }
    node = (uint16_t)(node - XX_LZPIS2_TREE_NODE_COUNT);
    if (node >= XX_LZPIS2_SYMBOL_COUNT ||
        !xx_lzpis2_huffman_update(tree, node)) {
        return false;
    }
    *symbol = node;
    return true;
}

static bool xx_lzpis2_decode_position(xx_lzpis2_bits *bits,
                                      unsigned *position) {
    static const uint16_t code_base[10] =
        {0U, 0U, 0U, 0U, 4U, 12U, 32U, 76U, 192U, 452U};
    static const uint8_t code_count[10] =
        {0U, 0U, 0U, 2U, 2U, 4U, 6U, 20U, 34U, 60U};
    static const uint8_t first_value[10] =
        {0U, 0U, 0U, 0U, 2U, 4U, 8U, 14U, 34U, 68U};
    uint32_t lookahead;
    unsigned code = 0U;
    unsigned width;
    if (!bits || !position) return false;
    lookahead = xx_lzpis2_peek_zero_padded(bits, 9U);
    for (width = 3U; width <= 9U; ++width) {
        unsigned candidate = lookahead >> (9U - width);
        unsigned start = code_base[width];
        if (candidate >= start &&
            candidate < start + code_count[width]) {
            uint32_t ignored;
            uint32_t low;
            if (!xx_lzpis2_get_bits(bits, width, &ignored) ||
                !xx_lzpis2_get_bits(bits, 6U, &low)) {
                return false;
            }
            code = (unsigned)first_value[width] + candidate - start;
            *position = (code << 6U) | (unsigned)low;
            return *position < XX_LZPIS2_WINDOW_SIZE;
        }
    }
    return false;
}

static bool xx_lzpis2_decode_chunk(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size) {
    xx_lzpis2_bits bits;
    xx_lzpis2_huffman tree;
    uint8_t window[XX_LZPIS2_WINDOW_SIZE];
    size_t ring_position = XX_LZPIS2_WINDOW_SIZE - XX_LZPIS2_MAX_MATCH;
    size_t output_position = 0U;

    if (!input || !output || input_size == 0U || output_size == 0U ||
        output_size > XX_LZPIS2_MAX_CHUNK_OUTPUT) {
        return false;
    }
    xx_rt_memset(&bits, 0, sizeof(bits));
    bits.data = input;
    bits.size = input_size;
    xx_rt_memset(window, 0, sizeof(window));
    xx_lzpis2_huffman_init(&tree);

    while (output_position < output_size) {
        unsigned symbol;
        if (!xx_lzpis2_huffman_decode_symbol(&tree, &bits, &symbol)) {
            return false;
        }
        if (symbol < 256U) {
            uint8_t value = (uint8_t)symbol;
            output[output_position++] = value;
            window[ring_position] = value;
            ring_position = (ring_position + 1U) &
                            (XX_LZPIS2_WINDOW_SIZE - 1U);
        } else {
            uint32_t extension = 0U;
            unsigned length = symbol - 255U + XX_LZPIS2_MATCH_THRESHOLD;
            unsigned position;
            size_t source;
            unsigned index;
            if (symbol == XX_LZPIS2_SYMBOL_COUNT - 1U &&
                !xx_lzpis2_get_bits(&bits, 8U, &extension)) {
                return false;
            }
            length += (unsigned)extension;
            if (length == 0U ||
                length > XX_LZPIS2_MAX_MATCH + 255U ||
                !xx_lzpis2_decode_position(&bits, &position)) {
                return false;
            }
            source = (ring_position + XX_LZPIS2_WINDOW_SIZE - position - 1U) &
                     (XX_LZPIS2_WINDOW_SIZE - 1U);
            for (index = 0U; index < length && output_position < output_size;
                 ++index) {
                uint8_t value = window[(source + index) &
                                       (XX_LZPIS2_WINDOW_SIZE - 1U)];
                output[output_position++] = value;
                window[ring_position] = value;
                ring_position = (ring_position + 1U) &
                                (XX_LZPIS2_WINDOW_SIZE - 1U);
            }
        }
        /* A well-formed chunk never reads outside its own compressed bytes. */
        if (xx_lzpis2_bits_overrun(&bits)) return false;
    }
    return output_position == output_size && !xx_lzpis2_bits_overrun(&bits);
}

bool xx_lzpis2_parse_memory(const uint8_t *input, size_t input_size,
                            xx_lzpis2_info *info) {
    size_t position = XX_LZPIS2_MAGIC_SIZE;
    uint64_t total_output = 0U;
    uint32_t count = 0U;
    if (info) xx_rt_memset(info, 0, sizeof(*info));
    if (!input || input_size > XX_LZPIS2_MAX_INPUT ||
        input_size < XX_LZPIS2_MAGIC_SIZE + XX_LZPIS2_CHUNK_HEADER_SIZE +
                         1U ||
        xx_rt_memcmp(input, "LZPIS2", XX_LZPIS2_MAGIC_SIZE) != 0) {
        return false;
    }
    while (position < input_size) {
        uint16_t unpacked_size;
        uint16_t packed_size;
        if (input_size - position < XX_LZPIS2_CHUNK_HEADER_SIZE) {
            return false;
        }
        unpacked_size = xx_lzpis2_read16le(input + position);
        packed_size = xx_lzpis2_read16le(input + position + 2U);
        position += XX_LZPIS2_CHUNK_HEADER_SIZE;
        if (unpacked_size == 0U ||
            unpacked_size > XX_LZPIS2_MAX_CHUNK_OUTPUT || packed_size == 0U ||
            (size_t)packed_size > input_size - position ||
            total_output > XX_LZPIS2_MAX_OUTPUT - (uint64_t)unpacked_size ||
            count == XX_LZPIS2_MAX_CHUNKS) {
            return false;
        }
        total_output += (uint64_t)unpacked_size;
        position += (size_t)packed_size;
        ++count;
    }
    if (position != input_size || count == 0U || total_output == 0U) {
        return false;
    }
    if (info) {
        info->uncompressed_size = total_output;
        info->chunk_count = count;
        info->archive_size = input_size;
    }
    return true;
}

bool xx_lzpis2_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size, xx_lzpis2_info *info) {
    xx_lzpis2_info parsed;
    size_t input_position = XX_LZPIS2_MAGIC_SIZE;
    size_t output_position = 0U;
    uint32_t index;
    if (consumed_size) *consumed_size = 0U;
    if (info) xx_rt_memset(info, 0, sizeof(*info));
    if (!input || !output ||
        !xx_lzpis2_parse_memory(input, input_size, &parsed) ||
        parsed.uncompressed_size > (uint64_t)SIZE_MAX ||
        output_size != (size_t)parsed.uncompressed_size) {
        return false;
    }
    for (index = 0U; index < parsed.chunk_count; ++index) {
        uint16_t unpacked_size = xx_lzpis2_read16le(input + input_position);
        uint16_t packed_size =
            xx_lzpis2_read16le(input + input_position + 2U);
        input_position += XX_LZPIS2_CHUNK_HEADER_SIZE;
        if ((size_t)unpacked_size > output_size - output_position ||
            !xx_lzpis2_decode_chunk(input + input_position,
                                     (size_t)packed_size,
                                     output + output_position,
                                     (size_t)unpacked_size)) {
            return false;
        }
        input_position += (size_t)packed_size;
        output_position += (size_t)unpacked_size;
    }
    if (input_position != input_size || output_position != output_size) {
        return false;
    }
    if (consumed_size) *consumed_size = input_position;
    if (info) *info = parsed;
    return true;
}
