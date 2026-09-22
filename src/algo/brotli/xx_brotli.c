/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Bounded, one-shot Brotli decoder written for xxfclib from the public RFC 7932
 * format description. It does not call, load, or compile another Brotli
 * implementation. The standardized dictionary and context lookup tables are
 * isolated in xx_brotli_data.c and retain the Brotli Authors' MIT attribution.
 */

#include "xxfclib/algo/brotli/xx_brotli.h"
#include "xxfclib/memory/xx_memory.h"
#include "xx_brotli_data.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define XX_BR_LITERAL_SYMBOLS 256U
#define XX_BR_COMMAND_SYMBOLS 704U
#define XX_BR_BLOCK_LENGTH_SYMBOLS 26U
#define XX_BR_MAX_BLOCK_TYPES 256U
#define XX_BR_MAX_HUFFMAN_BITS 15U
#define XX_BR_SHORT_DISTANCE_CODES 16U
#define XX_BR_DICTIONARY_SIZE 122784U
#define XX_BR_DICTIONARY_TRANSFORMS 121U
#define XX_BR_MAX_DISTANCE UINT32_C(0x7FFFFFFC)
#define XX_BR_MAX_ALLOCATED_OUTPUT ((size_t)1024U * 1024U * 1024U)

typedef struct xx_br_bits {
    const uint8_t *data;
    size_t bit_count;
    size_t position;
} xx_br_bits;

typedef struct xx_br_huff_node {
    int16_t child[2];
    int16_t symbol;
} xx_br_huff_node;

typedef struct xx_br_huff {
    xx_br_huff_node *nodes;
    uint16_t node_count;
    uint16_t capacity;
} xx_br_huff;

typedef struct xx_br_block {
    unsigned count;
    unsigned previous;
    unsigned current;
    size_t remaining;
    xx_br_huff type_tree;
    xx_br_huff length_tree;
} xx_br_block;

typedef struct xx_br_meta {
    xx_br_block blocks[3];
    uint8_t *context_modes;
    uint8_t *literal_map;
    uint8_t *distance_map;
    unsigned literal_tree_count;
    unsigned distance_tree_count;
    xx_br_huff *literal_trees;
    xx_br_huff *command_trees;
    xx_br_huff *distance_trees;
    unsigned postfix_bits;
    unsigned direct_codes;
    unsigned distance_alphabet;
} xx_br_meta;

typedef struct xx_br_decoder {
    xx_br_bits bits;
    uint8_t *output;
    size_t output_capacity;
    size_t output_position;
    bool grow_output;
    size_t maximum_output_capacity;
    unsigned window_bits;
    size_t maximum_backward_distance;
    int32_t distances[4];
    int distance_index;
} xx_br_decoder;

static bool xx_br_reserve_output(xx_br_decoder *decoder, size_t additional) {
    size_t required;
    size_t next_capacity;
    uint8_t *grown;
    if (!decoder || additional > SIZE_MAX - decoder->output_position) {
        return false;
    }
    required = decoder->output_position + additional;
    if (required <= decoder->output_capacity) return true;
    if (!decoder->grow_output || required > decoder->maximum_output_capacity) {
        return false;
    }
    next_capacity = decoder->output_capacity == 0U ? 64U :
                                                  decoder->output_capacity;
    while (next_capacity < required) {
        if (next_capacity > decoder->maximum_output_capacity / 2U) {
            next_capacity = decoder->maximum_output_capacity;
        } else {
            next_capacity *= 2U;
        }
        if (next_capacity < required &&
            next_capacity == decoder->maximum_output_capacity) {
            return false;
        }
    }
    grown = (uint8_t *)xx_mem_realloc(decoder->output, next_capacity);
    if (!grown) return false;
    decoder->output = grown;
    decoder->output_capacity = next_capacity;
    return true;
}

static bool xx_br_read_bits(xx_br_bits *bits, unsigned count,
                            uint32_t *result) {
    uint32_t value = 0U;
    if (!bits || !result || count > 32U ||
        count > bits->bit_count - bits->position) {
        return false;
    }
    for (unsigned index = 0; index < count; ++index) {
        size_t position = bits->position + index;
        value |= (uint32_t)((bits->data[position >> 3] >> (position & 7U)) & 1U)
                 << index;
    }
    bits->position += count;
    *result = value;
    return true;
}

static bool xx_br_peek_padded(const xx_br_bits *bits, unsigned count,
                              uint32_t *result, unsigned *available) {
    size_t left;
    unsigned actual;
    uint32_t value = 0U;
    if (!bits || !result || count > 32U) return false;
    left = bits->bit_count - bits->position;
    actual = left < count ? (unsigned)left : count;
    for (unsigned index = 0; index < actual; ++index) {
        size_t position = bits->position + index;
        value |= (uint32_t)((bits->data[position >> 3] >> (position & 7U)) & 1U)
                 << index;
    }
    *result = value;
    if (available) *available = actual;
    return true;
}

static bool xx_br_align_to_byte(xx_br_bits *bits) {
    unsigned remainder = (unsigned)(bits->position & 7U);
    uint32_t padding;
    if (remainder == 0U) return true;
    return xx_br_read_bits(bits, 8U - remainder, &padding) && padding == 0U;
}

static void xx_br_huff_free(xx_br_huff *tree) {
    if (!tree) return;
    xx_mem_free(tree->nodes);
    tree->nodes = NULL;
    tree->node_count = 0U;
    tree->capacity = 0U;
}

static void xx_br_node_init(xx_br_huff_node *node) {
    node->child[0] = -1;
    node->child[1] = -1;
    node->symbol = -1;
}

static bool xx_br_huff_add_node(xx_br_huff *tree, int16_t *index) {
    if (!tree || !index || tree->node_count >= tree->capacity ||
        tree->node_count > INT16_MAX) {
        return false;
    }
    *index = (int16_t)tree->node_count++;
    xx_br_node_init(&tree->nodes[(unsigned)*index]);
    return true;
}

static bool xx_br_huff_build(xx_br_huff *tree, const uint8_t *lengths,
                             unsigned alphabet_size, bool allow_single) {
    uint16_t counts[XX_BR_MAX_HUFFMAN_BITS + 1U] = {0};
    uint32_t next_code[XX_BR_MAX_HUFFMAN_BITS + 1U] = {0};
    unsigned used = 0U;
    unsigned single = 0U;
    int left = 1;
    uint32_t code = 0U;
    int16_t root;

    if (!tree || !lengths || alphabet_size == 0U || alphabet_size > 16383U ||
        alphabet_size > (UINT16_MAX - 1U) / 2U) {
        return false;
    }
    xx_br_huff_free(tree);
    for (unsigned symbol = 0; symbol < alphabet_size; ++symbol) {
        unsigned length = lengths[symbol];
        if (length > XX_BR_MAX_HUFFMAN_BITS) return false;
        if (length != 0U) {
            ++counts[length];
            ++used;
            single = symbol;
        }
    }
    if (used == 0U || (used == 1U && !allow_single)) return false;
    tree->capacity = (uint16_t)(alphabet_size * 2U + 1U);
    tree->nodes = (xx_br_huff_node *)xx_mem_alloc(
        (size_t)tree->capacity * sizeof(*tree->nodes));
    if (!tree->nodes || !xx_br_huff_add_node(tree, &root) || root != 0) {
        xx_br_huff_free(tree);
        return false;
    }
    if (used == 1U) {
        tree->nodes[0].symbol = (int16_t)single;
        return true;
    }

    for (unsigned length = 1U; length <= XX_BR_MAX_HUFFMAN_BITS; ++length) {
        left <<= 1;
        left -= counts[length];
        if (left < 0) {
            xx_br_huff_free(tree);
            return false;
        }
    }
    if (left != 0) {
        xx_br_huff_free(tree);
        return false;
    }
    for (unsigned length = 1U; length <= XX_BR_MAX_HUFFMAN_BITS; ++length) {
        code = (code + counts[length - 1U]) << 1;
        next_code[length] = code;
    }
    for (unsigned symbol = 0; symbol < alphabet_size; ++symbol) {
        unsigned length = lengths[symbol];
        uint32_t symbol_code;
        int16_t node = 0;
        if (length == 0U) continue;
        symbol_code = next_code[length]++;
        for (unsigned depth = 0U; depth < length; ++depth) {
            unsigned bit = (unsigned)((symbol_code >>
                                      (length - depth - 1U)) & 1U);
            int16_t child = tree->nodes[(unsigned)node].child[bit];
            if (tree->nodes[(unsigned)node].symbol >= 0) {
                xx_br_huff_free(tree);
                return false;
            }
            if (child < 0) {
                if (!xx_br_huff_add_node(tree, &child)) {
                    xx_br_huff_free(tree);
                    return false;
                }
                tree->nodes[(unsigned)node].child[bit] = child;
            }
            node = child;
        }
        if (tree->nodes[(unsigned)node].symbol >= 0 ||
            tree->nodes[(unsigned)node].child[0] >= 0 ||
            tree->nodes[(unsigned)node].child[1] >= 0) {
            xx_br_huff_free(tree);
            return false;
        }
        tree->nodes[(unsigned)node].symbol = (int16_t)symbol;
    }
    return true;
}

static bool xx_br_huff_decode(xx_br_bits *bits, const xx_br_huff *tree,
                              unsigned *symbol) {
    int16_t node = 0;
    if (!bits || !tree || !tree->nodes || !symbol) return false;
    for (unsigned depth = 0U; depth <= XX_BR_MAX_HUFFMAN_BITS; ++depth) {
        const xx_br_huff_node *entry = &tree->nodes[(unsigned)node];
        uint32_t bit;
        if (entry->symbol >= 0) {
            *symbol = (unsigned)entry->symbol;
            return true;
        }
        if (depth == XX_BR_MAX_HUFFMAN_BITS ||
            !xx_br_read_bits(bits, 1U, &bit) || entry->child[bit] < 0) {
            return false;
        }
        node = entry->child[bit];
    }
    return false;
}

static unsigned xx_br_symbol_bits(unsigned alphabet_size) {
    unsigned bits = 0U;
    unsigned value = alphabet_size - 1U;
    while (value != 0U) {
        ++bits;
        value >>= 1;
    }
    return bits;
}

static bool xx_br_read_huffman(xx_br_bits *bits, unsigned alphabet_max,
                               unsigned alphabet_limit, xx_br_huff *tree) {
    static const uint8_t order[18] = {
        1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    static const uint8_t prefix_length[16] = {
        2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 4
    };
    static const uint8_t prefix_value[16] = {
        0, 4, 3, 2, 0, 4, 3, 1, 0, 4, 3, 2, 0, 4, 3, 5
    };
    uint8_t code_length_lengths[18] = {0};
    uint8_t *lengths = NULL;
    xx_br_huff code_length_tree = {0};
    uint32_t mode;
    bool success = false;

    if (!bits || !tree || alphabet_max == 0U ||
        alphabet_limit == 0U || alphabet_limit > alphabet_max ||
        !xx_br_read_bits(bits, 2U, &mode)) {
        return false;
    }
    lengths = (uint8_t *)xx_mem_calloc(alphabet_limit, sizeof(*lengths));
    if (!lengths) return false;

    if (mode == 1U) {
        uint32_t size_code;
        unsigned symbols[4];
        unsigned count;
        unsigned symbol_bits = xx_br_symbol_bits(alphabet_max);
        if (!xx_br_read_bits(bits, 2U, &size_code)) goto cleanup;
        count = (unsigned)size_code + 1U;
        for (unsigned index = 0U; index < count; ++index) {
            uint32_t value;
            if (!xx_br_read_bits(bits, symbol_bits, &value) ||
                value >= alphabet_limit) {
                goto cleanup;
            }
            symbols[index] = (unsigned)value;
            for (unsigned previous = 0U; previous < index; ++previous) {
                if (symbols[previous] == symbols[index]) goto cleanup;
            }
        }
        if (count == 1U) {
            lengths[symbols[0]] = 1U;
        } else if (count == 2U) {
            lengths[symbols[0]] = 1U;
            lengths[symbols[1]] = 1U;
        } else if (count == 3U) {
            lengths[symbols[0]] = 1U;
            lengths[symbols[1]] = 2U;
            lengths[symbols[2]] = 2U;
        } else {
            uint32_t selector;
            if (!xx_br_read_bits(bits, 1U, &selector)) goto cleanup;
            if (selector == 0U) {
                for (unsigned index = 0U; index < 4U; ++index) {
                    lengths[symbols[index]] = 2U;
                }
            } else {
                lengths[symbols[0]] = 1U;
                lengths[symbols[1]] = 2U;
                lengths[symbols[2]] = 3U;
                lengths[symbols[3]] = 3U;
            }
        }
        success = xx_br_huff_build(tree, lengths, alphabet_limit,
                                   count == 1U);
        goto cleanup;
    }

    {
        int space = 32;
        unsigned nonzero = 0U;
        unsigned start = (unsigned)mode;
        for (unsigned index = start; index < 18U; ++index) {
            uint32_t prefix;
            unsigned available;
            unsigned length;
            unsigned value;
            if (!xx_br_peek_padded(bits, 4U, &prefix, &available)) goto cleanup;
            length = prefix_length[prefix & 15U];
            value = prefix_value[prefix & 15U];
            if (length > available || !xx_br_read_bits(bits, length, &prefix)) {
                goto cleanup;
            }
            code_length_lengths[order[index]] = (uint8_t)value;
            if (value != 0U) {
                space -= 32 >> value;
                ++nonzero;
                if (space < 0) goto cleanup;
                if (space == 0) break;
            }
        }
        if (nonzero != 1U && space != 0) goto cleanup;
        if (!xx_br_huff_build(&code_length_tree, code_length_lengths, 18U,
                              nonzero == 1U)) {
            goto cleanup;
        }
    }

    {
        size_t position = 0U;
        unsigned previous_length = 8U;
        unsigned repeat = 0U;
        unsigned repeat_length = 0U;
        int space = 32768;
        while (position < alphabet_limit && space > 0) {
            unsigned value;
            if (!xx_br_huff_decode(bits, &code_length_tree, &value)) goto cleanup;
            if (value < 16U) {
                lengths[position++] = (uint8_t)value;
                repeat = 0U;
                if (value != 0U) {
                    previous_length = value;
                    space -= 32768 >> value;
                    if (space < 0) goto cleanup;
                }
            } else {
                unsigned extra_bits = value == 16U ? 2U : 3U;
                unsigned new_length = value == 16U ? previous_length : 0U;
                unsigned old_repeat;
                unsigned delta;
                uint32_t extra;
                if (value > 17U || !xx_br_read_bits(bits, extra_bits, &extra)) {
                    goto cleanup;
                }
                if (repeat_length != new_length) {
                    repeat = 0U;
                    repeat_length = new_length;
                }
                old_repeat = repeat;
                if (repeat != 0U) repeat = (repeat - 2U) << extra_bits;
                if (repeat > UINT_MAX - (unsigned)extra - 3U) goto cleanup;
                repeat += (unsigned)extra + 3U;
                delta = repeat - old_repeat;
                if (delta > alphabet_limit - position) goto cleanup;
                for (unsigned index = 0U; index < delta; ++index) {
                    lengths[position++] = (uint8_t)new_length;
                }
                if (new_length != 0U) {
                    space -= (int)(delta << (15U - new_length));
                    if (space < 0) goto cleanup;
                }
            }
        }
        if (space != 0) goto cleanup;
    }
    success = xx_br_huff_build(tree, lengths, alphabet_limit, false);

cleanup:
    xx_br_huff_free(&code_length_tree);
    xx_mem_free(lengths);
    return success;
}

static bool xx_br_read_var_uint8(xx_br_bits *bits, unsigned *value) {
    uint32_t present;
    uint32_t width;
    uint32_t suffix;
    if (!bits || !value || !xx_br_read_bits(bits, 1U, &present)) return false;
    if (present == 0U) {
        *value = 0U;
        return true;
    }
    if (!xx_br_read_bits(bits, 3U, &width)) return false;
    if (width == 0U) {
        *value = 1U;
        return true;
    }
    if (!xx_br_read_bits(bits, (unsigned)width, &suffix)) return false;
    *value = (1U << width) + (unsigned)suffix;
    return true;
}

static bool xx_br_read_block_length(xx_br_bits *bits,
                                    const xx_br_huff *tree,
                                    size_t *length) {
    static const uint32_t offsets[XX_BR_BLOCK_LENGTH_SYMBOLS] = {
        1U, 5U, 9U, 13U, 17U, 25U, 33U, 41U, 49U, 65U, 81U,
        97U, 113U, 145U, 177U, 209U, 241U, 305U, 369U, 497U,
        753U, 1265U, 2289U, 4337U, 8433U, 16625U
    };
    static const uint8_t extra_bits[XX_BR_BLOCK_LENGTH_SYMBOLS] = {
        2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5,
        5, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 24
    };
    unsigned symbol;
    uint32_t extra;
    if (!xx_br_huff_decode(bits, tree, &symbol) ||
        symbol >= XX_BR_BLOCK_LENGTH_SYMBOLS ||
        !xx_br_read_bits(bits, extra_bits[symbol], &extra)) {
        return false;
    }
    *length = (size_t)offsets[symbol] + (size_t)extra;
    return true;
}

static void xx_br_block_free(xx_br_block *block) {
    if (!block) return;
    xx_br_huff_free(&block->type_tree);
    xx_br_huff_free(&block->length_tree);
}

static bool xx_br_block_init(xx_br_bits *bits, xx_br_block *block) {
    unsigned count_minus_one;
    if (!bits || !block ||
        !xx_br_read_var_uint8(bits, &count_minus_one) ||
        count_minus_one >= XX_BR_MAX_BLOCK_TYPES) {
        return false;
    }
    block->count = count_minus_one + 1U;
    block->previous = 1U;
    block->current = 0U;
    block->remaining = SIZE_MAX;
    if (block->count == 1U) return true;
    if (!xx_br_read_huffman(bits, block->count + 2U,
                            block->count + 2U, &block->type_tree) ||
        !xx_br_read_huffman(bits, XX_BR_BLOCK_LENGTH_SYMBOLS,
                            XX_BR_BLOCK_LENGTH_SYMBOLS,
                            &block->length_tree) ||
        !xx_br_read_block_length(bits, &block->length_tree,
                                 &block->remaining)) {
        return false;
    }
    return true;
}

static bool xx_br_block_switch(xx_br_bits *bits, xx_br_block *block) {
    unsigned code;
    unsigned next;
    size_t length;
    if (!bits || !block || block->count <= 1U) return false;
    if (!xx_br_huff_decode(bits, &block->type_tree, &code) ||
        !xx_br_read_block_length(bits, &block->length_tree, &length)) {
        return false;
    }
    if (code == 0U) {
        next = block->previous;
    } else if (code == 1U) {
        next = block->current + 1U;
    } else {
        next = code - 2U;
    }
    if (next >= block->count) next -= block->count;
    if (next >= block->count) return false;
    block->previous = block->current;
    block->current = next;
    block->remaining = length;
    return true;
}

static bool xx_br_block_prepare(xx_br_bits *bits, xx_br_block *block) {
    if (block->count == 1U) return true;
    if (block->remaining == 0U && !xx_br_block_switch(bits, block)) {
        return false;
    }
    return block->remaining != 0U;
}

static void xx_br_block_consume(xx_br_block *block) {
    if (block->count > 1U && block->remaining != 0U) --block->remaining;
}

static bool xx_br_decode_context_map(xx_br_bits *bits, size_t map_size,
                                     uint8_t **out_map,
                                     unsigned *out_tree_count) {
    xx_br_huff tree = {0};
    uint8_t *map = NULL;
    unsigned count_minus_one;
    unsigned tree_count;
    uint32_t use_runs;
    unsigned max_run = 0U;
    bool success = false;

    if (!bits || !out_map || !out_tree_count || map_size == 0U ||
        !xx_br_read_var_uint8(bits, &count_minus_one) ||
        count_minus_one >= XX_BR_MAX_BLOCK_TYPES) {
        return false;
    }
    tree_count = count_minus_one + 1U;
    map = (uint8_t *)xx_mem_calloc(map_size, sizeof(*map));
    if (!map) return false;
    if (tree_count == 1U) {
        *out_map = map;
        *out_tree_count = tree_count;
        return true;
    }
    if (!xx_br_read_bits(bits, 1U, &use_runs)) goto cleanup;
    if (use_runs != 0U) {
        uint32_t encoded;
        if (!xx_br_read_bits(bits, 4U, &encoded)) goto cleanup;
        max_run = (unsigned)encoded + 1U;
    }
    if (!xx_br_read_huffman(bits, tree_count + max_run,
                            tree_count + max_run, &tree)) {
        goto cleanup;
    }
    for (size_t position = 0U; position < map_size;) {
        unsigned code;
        if (!xx_br_huff_decode(bits, &tree, &code)) goto cleanup;
        if (code == 0U) {
            map[position++] = 0U;
        } else if (code > max_run) {
            unsigned value = code - max_run;
            if (value >= tree_count) goto cleanup;
            map[position++] = (uint8_t)value;
        } else {
            uint32_t suffix;
            size_t run;
            if (!xx_br_read_bits(bits, code, &suffix)) goto cleanup;
            run = ((size_t)1U << code) + (size_t)suffix;
            if (run > map_size - position) goto cleanup;
            position += run;
        }
    }
    {
        uint32_t use_mtf;
        if (!xx_br_read_bits(bits, 1U, &use_mtf)) goto cleanup;
        if (use_mtf != 0U) {
            uint8_t mtf[XX_BR_MAX_BLOCK_TYPES];
            for (unsigned index = 0U; index < tree_count; ++index) {
                mtf[index] = (uint8_t)index;
            }
            for (size_t index = 0U; index < map_size; ++index) {
                unsigned position = map[index];
                uint8_t value;
                if (position >= tree_count) goto cleanup;
                value = mtf[position];
                while (position != 0U) {
                    mtf[position] = mtf[position - 1U];
                    --position;
                }
                mtf[0] = value;
                map[index] = value;
            }
        }
    }
    *out_map = map;
    *out_tree_count = tree_count;
    map = NULL;
    success = true;

cleanup:
    xx_br_huff_free(&tree);
    xx_mem_free(map);
    return success;
}

static bool xx_br_read_tree_group(xx_br_bits *bits, unsigned tree_count,
                                  unsigned alphabet_max,
                                  unsigned alphabet_limit,
                                  xx_br_huff **out_trees) {
    xx_br_huff *trees;
    if (!bits || !out_trees || tree_count == 0U ||
        tree_count > XX_BR_MAX_BLOCK_TYPES) {
        return false;
    }
    trees = (xx_br_huff *)xx_mem_calloc(tree_count, sizeof(*trees));
    if (!trees) return false;
    for (unsigned index = 0U; index < tree_count; ++index) {
        if (!xx_br_read_huffman(bits, alphabet_max, alphabet_limit,
                                &trees[index])) {
            for (unsigned done = 0U; done < index; ++done) {
                xx_br_huff_free(&trees[done]);
            }
            xx_mem_free(trees);
            return false;
        }
    }
    *out_trees = trees;
    return true;
}

static void xx_br_tree_group_free(xx_br_huff *trees, unsigned count) {
    if (!trees) return;
    for (unsigned index = 0U; index < count; ++index) {
        xx_br_huff_free(&trees[index]);
    }
    xx_mem_free(trees);
}

static void xx_br_meta_free(xx_br_meta *meta) {
    if (!meta) return;
    for (unsigned index = 0U; index < 3U; ++index) {
        xx_br_block_free(&meta->blocks[index]);
    }
    xx_mem_free(meta->context_modes);
    xx_mem_free(meta->literal_map);
    xx_mem_free(meta->distance_map);
    xx_br_tree_group_free(meta->literal_trees, meta->literal_tree_count);
    xx_br_tree_group_free(meta->command_trees,
                          meta->blocks[1].count);
    xx_br_tree_group_free(meta->distance_trees, meta->distance_tree_count);
    meta->context_modes = NULL;
    meta->literal_map = NULL;
    meta->distance_map = NULL;
    meta->literal_trees = NULL;
    meta->command_trees = NULL;
    meta->distance_trees = NULL;
}

static bool xx_br_distance_limit(unsigned postfix, unsigned direct,
                                 unsigned *alphabet_limit) {
    uint32_t max_distance = XX_BR_MAX_DISTANCE;
    uint32_t forbidden;
    uint32_t offset;
    uint32_t distance_bits = 0U;
    uint32_t half;
    uint32_t group;
    uint32_t postfix_mask;
    if (!alphabet_limit || postfix > 3U || direct > 120U) return false;
    if (max_distance <= direct) {
        *alphabet_limit = (unsigned)max_distance + XX_BR_SHORT_DISTANCE_CODES;
        return true;
    }
    forbidden = max_distance + 1U;
    offset = ((forbidden - direct - 1U) >> postfix) + 4U;
    for (uint32_t value = offset / 2U; value != 0U; value >>= 1U) {
        ++distance_bits;
    }
    if (distance_bits == 0U) return false;
    --distance_bits;
    half = (offset >> distance_bits) & 1U;
    group = ((distance_bits - 1U) << 1U) | half;
    if (group == 0U) {
        *alphabet_limit = direct + XX_BR_SHORT_DISTANCE_CODES;
        return true;
    }
    --group;
    postfix_mask = (1U << postfix) - 1U;
    *alphabet_limit = (unsigned)(((group << postfix) | postfix_mask) + direct +
                                 XX_BR_SHORT_DISTANCE_CODES + 1U);
    return true;
}

static bool xx_br_meta_read(xx_br_decoder *decoder, xx_br_meta *meta) {
    uint32_t distance_parameters;
    unsigned distance_limit;
    if (!decoder || !meta) return false;
    for (unsigned index = 0U; index < 3U; ++index) {
        if (!xx_br_block_init(&decoder->bits, &meta->blocks[index])) {
            return false;
        }
    }
    if (!xx_br_read_bits(&decoder->bits, 6U, &distance_parameters)) {
        return false;
    }
    meta->postfix_bits = (unsigned)distance_parameters & 3U;
    meta->direct_codes = ((unsigned)distance_parameters >> 2U)
                         << meta->postfix_bits;
    if (meta->postfix_bits > 3U || meta->direct_codes > 120U) return false;
    meta->context_modes = (uint8_t *)xx_mem_alloc(meta->blocks[0].count);
    if (!meta->context_modes) return false;
    for (unsigned index = 0U; index < meta->blocks[0].count; ++index) {
        uint32_t mode;
        if (!xx_br_read_bits(&decoder->bits, 2U, &mode)) return false;
        meta->context_modes[index] = (uint8_t)mode;
    }
    if (!xx_br_decode_context_map(
            &decoder->bits, (size_t)meta->blocks[0].count * 64U,
            &meta->literal_map, &meta->literal_tree_count) ||
        !xx_br_decode_context_map(
            &decoder->bits, (size_t)meta->blocks[2].count * 4U,
            &meta->distance_map, &meta->distance_tree_count)) {
        return false;
    }
    if (decoder->window_bits <= 24U) {
        meta->distance_alphabet = XX_BR_SHORT_DISTANCE_CODES +
            meta->direct_codes + (24U << (meta->postfix_bits + 1U));
        distance_limit = meta->distance_alphabet;
    } else {
        meta->distance_alphabet = XX_BR_SHORT_DISTANCE_CODES +
            meta->direct_codes + (62U << (meta->postfix_bits + 1U));
        if (!xx_br_distance_limit(meta->postfix_bits, meta->direct_codes,
                                  &distance_limit) ||
            distance_limit > meta->distance_alphabet) {
            return false;
        }
    }
    return xx_br_read_tree_group(&decoder->bits, meta->literal_tree_count,
                                 XX_BR_LITERAL_SYMBOLS,
                                 XX_BR_LITERAL_SYMBOLS,
                                 &meta->literal_trees) &&
           xx_br_read_tree_group(&decoder->bits, meta->blocks[1].count,
                                 XX_BR_COMMAND_SYMBOLS,
                                 XX_BR_COMMAND_SYMBOLS,
                                 &meta->command_trees) &&
           xx_br_read_tree_group(&decoder->bits, meta->distance_tree_count,
                                 meta->distance_alphabet, distance_limit,
                                 &meta->distance_trees);
}

static bool xx_br_command(unsigned symbol, xx_br_bits *bits,
                          size_t *insert_length, size_t *copy_length,
                          unsigned *distance_context,
                          bool *implicit_distance) {
    static const uint8_t insert_extra[24] = {
        0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
        4, 4, 5, 5, 6, 7, 8, 9, 10, 12, 14, 24
    };
    static const uint8_t copy_extra[24] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2,
        3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 24
    };
    static const uint8_t cell_position[11] = {
        0, 1, 0, 1, 8, 9, 2, 16, 10, 17, 18
    };
    uint32_t insert_offsets[24];
    uint32_t copy_offsets[24];
    unsigned cell;
    unsigned position;
    unsigned insert_code;
    unsigned copy_code;
    uint32_t insert_suffix;
    uint32_t copy_suffix;

    if (!bits || !insert_length || !copy_length || !distance_context ||
        !implicit_distance || symbol >= XX_BR_COMMAND_SYMBOLS) {
        return false;
    }
    insert_offsets[0] = 0U;
    copy_offsets[0] = 2U;
    for (unsigned index = 0U; index < 23U; ++index) {
        insert_offsets[index + 1U] = insert_offsets[index] +
                                     (UINT32_C(1) << insert_extra[index]);
        copy_offsets[index + 1U] = copy_offsets[index] +
                                   (UINT32_C(1) << copy_extra[index]);
    }
    cell = symbol >> 6U;
    position = cell_position[cell];
    copy_code = ((position << 3U) & 0x18U) + (symbol & 7U);
    insert_code = (position & 0x18U) + ((symbol >> 3U) & 7U);
    if (!xx_br_read_bits(bits, insert_extra[insert_code], &insert_suffix) ||
        !xx_br_read_bits(bits, copy_extra[copy_code], &copy_suffix)) {
        return false;
    }
    *insert_length = (size_t)insert_offsets[insert_code] + insert_suffix;
    *copy_length = (size_t)copy_offsets[copy_code] + copy_suffix;
    *distance_context = copy_offsets[copy_code] > 4U
                            ? 3U
                            : copy_offsets[copy_code] - 2U;
    *implicit_distance = cell < 2U;
    return true;
}

static unsigned xx_br_literal_context(const xx_br_decoder *decoder,
                                      unsigned mode) {
    uint8_t previous = decoder->output_position == 0U
                           ? 0U
                           : decoder->output[decoder->output_position - 1U];
    uint8_t before_previous = decoder->output_position < 2U
                                  ? 0U
                                  : decoder->output[decoder->output_position - 2U];
    size_t offset = (size_t)(mode & 3U) << 9U;
    return (unsigned)(xx_brotli_context_table[offset + previous] |
                      xx_brotli_context_table[offset + 256U + before_previous]);
}

static int xx_br_uppercase(uint8_t *text, size_t length) {
    if (length == 0U) return 0;
    if (text[0] < UINT8_C(0xC0)) {
        if (text[0] >= (uint8_t)'a' && text[0] <= (uint8_t)'z') {
            text[0] ^= UINT8_C(32);
        }
        return 1;
    }
    if (text[0] < UINT8_C(0xE0)) {
        if (length < 2U) return 1;
        text[1] ^= UINT8_C(32);
        return 2;
    }
    if (length < 3U) return (int)length;
    text[2] ^= UINT8_C(5);
    return 3;
}

static bool xx_br_transform_word(unsigned transform_index,
                                 const uint8_t *word, size_t word_length,
                                 uint8_t transformed[64],
                                 size_t *transformed_length) {
    static const uint8_t prefix_suffix[] =
        "\1 \2, \10 of the \4 of \2s \1.\5 and \4 "
        "in \1\"\4 to \2\">\1\n\2. \1]\5 for \3 a \6 "
        "that \1\'\6 with \6 from \4 by \1(\6. T"
        "he \4 on \4 as \4 is \4ing \2\n\t\1:\3ed "
        "\2=\"\4 at \3ly \1,\2=\'\5.com/\7. This \5"
        " not \3er \3al \4ful \4ive \5less \4es"
        "t \4ize \2\302\240\4ous \5 the \2e ";
    static const uint16_t map[50] = {
        0x00, 0x02, 0x05, 0x0E, 0x13, 0x16, 0x18, 0x1E, 0x23, 0x25,
        0x2A, 0x2D, 0x2F, 0x32, 0x34, 0x3A, 0x3E, 0x45, 0x47, 0x4E,
        0x55, 0x5A, 0x5C, 0x63, 0x68, 0x6D, 0x72, 0x77, 0x7A, 0x7C,
        0x80, 0x83, 0x88, 0x8C, 0x8E, 0x91, 0x97, 0x9F, 0xA5, 0xA9,
        0xAD, 0xB2, 0xB7, 0xBD, 0xC2, 0xC7, 0xCA, 0xCF, 0xD5, 0xD8
    };
    const uint8_t *triple;
    const uint8_t *prefix;
    const uint8_t *suffix;
    unsigned type;
    size_t prefix_length;
    size_t suffix_length;
    size_t skip = 0U;
    size_t body_length = word_length;
    size_t output = 0U;

    if (!word || !transformed || !transformed_length ||
        transform_index >= XX_BR_DICTIONARY_TRANSFORMS) {
        return false;
    }
    triple = &xx_brotli_transforms[transform_index * 3U];
    if (triple[0] >= 50U || triple[2] >= 50U) return false;
    prefix = &prefix_suffix[map[triple[0]]];
    suffix = &prefix_suffix[map[triple[2]]];
    prefix_length = *prefix++;
    suffix_length = *suffix++;
    type = triple[1];
    if (type <= 9U) {
        body_length = type >= body_length ? 0U : body_length - type;
    } else if (type >= 12U && type <= 20U) {
        skip = type - 11U;
        if (skip >= body_length) {
            skip = body_length;
            body_length = 0U;
        } else {
            body_length -= skip;
        }
    } else if (type != 10U && type != 11U) {
        return false;
    }
    if (prefix_length + body_length + suffix_length > 64U) return false;
    for (size_t index = 0U; index < prefix_length; ++index) {
        transformed[output++] = prefix[index];
    }
    for (size_t index = 0U; index < body_length; ++index) {
        transformed[output++] = word[skip + index];
    }
    if (type == 10U && body_length != 0U) {
        (void)xx_br_uppercase(transformed + prefix_length, body_length);
    } else if (type == 11U) {
        size_t position = 0U;
        while (position < body_length) {
            int step = xx_br_uppercase(transformed + prefix_length + position,
                                       body_length - position);
            if (step <= 0 || (size_t)step > body_length - position) return false;
            position += (size_t)step;
        }
    }
    for (size_t index = 0U; index < suffix_length; ++index) {
        transformed[output++] = suffix[index];
    }
    *transformed_length = output;
    return true;
}

static bool xx_br_copy_dictionary(xx_br_decoder *decoder, size_t distance,
                                  size_t maximum_distance,
                                  size_t copy_length, size_t remaining,
                                  unsigned distance_context,
                                  size_t *produced) {
    static const uint8_t size_bits[32] = {
        0, 0, 0, 0, 10, 10, 11, 11, 10, 10, 10, 10, 10, 9, 9, 8,
        7, 7, 8, 7, 7, 6, 6, 5, 5, 0, 0, 0, 0, 0, 0, 0
    };
    static const uint32_t offsets[32] = {
        0, 0, 0, 0, 0, 4096, 9216, 21504, 35840, 44032, 53248,
        63488, 74752, 87040, 93696, 100864, 104704, 106752,
        108928, 113536, 115968, 118528, 119872, 121280, 122016,
        122784, 122784, 122784, 122784, 122784, 122784, 122784
    };
    size_t address;
    unsigned bits;
    size_t word_index;
    unsigned transform_index;
    size_t dictionary_offset;
    uint8_t transformed[64];
    size_t transformed_size;

    if (!decoder || !produced || distance <= maximum_distance ||
        distance > XX_BR_MAX_DISTANCE || copy_length < 4U ||
        copy_length > 24U) {
        return false;
    }
    bits = size_bits[copy_length];
    if (bits == 0U) return false;
    address = distance - maximum_distance - 1U;
    word_index = address & (((size_t)1U << bits) - 1U);
    transform_index = (unsigned)(address >> bits);
    if (transform_index >= XX_BR_DICTIONARY_TRANSFORMS ||
        word_index > (SIZE_MAX - offsets[copy_length]) / copy_length) {
        return false;
    }
    dictionary_offset = (size_t)offsets[copy_length] +
                        word_index * copy_length;
    if (dictionary_offset > XX_BR_DICTIONARY_SIZE - copy_length ||
        !xx_br_transform_word(transform_index,
                              xx_brotli_dictionary_data + dictionary_offset,
                              copy_length, transformed, &transformed_size) ||
        (transformed_size == 0U && distance <= 120U) ||
        transformed_size > remaining ||
        !xx_br_reserve_output(decoder, transformed_size)) {
        return false;
    }
    for (size_t index = 0U; index < transformed_size; ++index) {
        decoder->output[decoder->output_position++] = transformed[index];
    }
    decoder->distance_index += (int)distance_context;
    *produced = transformed_size;
    return true;
}

static bool xx_br_resolve_distance(xx_br_decoder *decoder, xx_br_meta *meta,
                                   unsigned distance_context,
                                   bool implicit_distance,
                                   size_t *distance,
                                   unsigned *restore_context) {
    unsigned code;
    int32_t resolved;
    *restore_context = 0U;
    if (implicit_distance) {
        --decoder->distance_index;
        resolved = decoder->distances[(unsigned)decoder->distance_index & 3U];
        *restore_context = 1U;
    } else {
        xx_br_block *block = &meta->blocks[2];
        unsigned tree_index;
        if (!xx_br_block_prepare(&decoder->bits, block)) return false;
        tree_index = meta->distance_map[(size_t)block->current * 4U +
                                        distance_context];
        if (tree_index >= meta->distance_tree_count ||
            !xx_br_huff_decode(&decoder->bits,
                               &meta->distance_trees[tree_index], &code)) {
            return false;
        }
        xx_br_block_consume(block);
        if (code < 16U) {
            if (code <= 3U) {
                int offset = (int)code - 3;
                *restore_context = 1U >> code;
                resolved = decoder->distances[
                    (unsigned)(decoder->distance_index - offset) & 3U];
                decoder->distance_index -= (int)*restore_context;
            } else {
                int index_delta = code < 10U ? 3 : 2;
                int base = code < 10U ? (int)code - 4 : (int)code - 10;
                int delta = (int)((UINT32_C(0x605142) >> (4 * base)) & 15U) - 3;
                resolved = decoder->distances[
                               (unsigned)(decoder->distance_index + index_delta) & 3U] +
                           delta;
            }
        } else if (code < 16U + meta->direct_codes) {
            resolved = (int32_t)(code - 15U);
        } else {
            unsigned postfix_count = 1U << meta->postfix_bits;
            unsigned adjusted = code - 16U - meta->direct_codes;
            unsigned group = adjusted >> meta->postfix_bits;
            unsigned postfix = adjusted & (postfix_count - 1U);
            unsigned extra_bits = (group >> 1U) + 1U;
            unsigned half = group & 1U;
            uint64_t base = (uint64_t)meta->direct_codes +
                ((((uint64_t)(2U + half) << extra_bits) - 4U)
                 << meta->postfix_bits) + 1U + postfix;
            uint32_t extra;
            uint64_t value;
            if (extra_bits > 31U ||
                !xx_br_read_bits(&decoder->bits, extra_bits, &extra)) {
                return false;
            }
            value = base + ((uint64_t)extra << meta->postfix_bits);
            if (value > XX_BR_MAX_DISTANCE) return false;
            resolved = (int32_t)value;
        }
    }
    if (resolved <= 0) return false;
    *distance = (size_t)resolved;
    return true;
}

static bool xx_br_decode_compressed_meta(xx_br_decoder *decoder,
                                         size_t meta_length) {
    xx_br_meta meta = {0};
    size_t remaining = meta_length;
    bool success = false;

    if (!xx_br_meta_read(decoder, &meta)) goto cleanup;
    while (remaining != 0U) {
        xx_br_block *command_block = &meta.blocks[1];
        unsigned command_symbol;
        size_t insert_length;
        size_t copy_length;
        unsigned distance_context;
        bool implicit_distance;

        if (!xx_br_block_prepare(&decoder->bits, command_block) ||
            !xx_br_huff_decode(
                &decoder->bits,
                &meta.command_trees[command_block->current],
                &command_symbol)) {
            goto cleanup;
        }
        xx_br_block_consume(command_block);
        if (!xx_br_command(command_symbol, &decoder->bits,
                           &insert_length, &copy_length,
                           &distance_context, &implicit_distance) ||
            insert_length > remaining ||
            !xx_br_reserve_output(decoder, insert_length)) {
            goto cleanup;
        }
        remaining -= insert_length;
        for (size_t inserted = 0U; inserted < insert_length; ++inserted) {
            xx_br_block *literal_block = &meta.blocks[0];
            unsigned context;
            unsigned tree_index;
            unsigned literal;
            if (!xx_br_block_prepare(&decoder->bits, literal_block)) {
                goto cleanup;
            }
            context = xx_br_literal_context(
                decoder, meta.context_modes[literal_block->current]);
            tree_index = meta.literal_map[
                (size_t)literal_block->current * 64U + context];
            if (tree_index >= meta.literal_tree_count ||
                !xx_br_huff_decode(&decoder->bits,
                                   &meta.literal_trees[tree_index],
                                   &literal) ||
                literal >= XX_BR_LITERAL_SYMBOLS) {
                goto cleanup;
            }
            xx_br_block_consume(literal_block);
            decoder->output[decoder->output_position++] = (uint8_t)literal;
        }
        if (remaining == 0U) break;
        {
            size_t distance;
            unsigned restore_context;
            size_t maximum_distance = decoder->output_position <
                                               decoder->maximum_backward_distance
                                           ? decoder->output_position
                                           : decoder->maximum_backward_distance;
            if (!xx_br_resolve_distance(decoder, &meta, distance_context,
                                        implicit_distance, &distance,
                                        &restore_context)) {
                goto cleanup;
            }
            if (distance > maximum_distance) {
                size_t produced;
                if (!xx_br_copy_dictionary(decoder, distance,
                                           maximum_distance, copy_length,
                                           remaining, restore_context,
                                           &produced)) {
                    goto cleanup;
                }
                remaining -= produced;
            } else {
                size_t source_position;
            if (copy_length > remaining ||
                !xx_br_reserve_output(decoder, copy_length) ||
                distance == 0U || distance > decoder->output_position) {
                    goto cleanup;
                }
                source_position = decoder->output_position - distance;
                for (size_t copied = 0U; copied < copy_length; ++copied) {
                    decoder->output[decoder->output_position++] =
                        decoder->output[source_position++];
                }
                decoder->distances[(unsigned)decoder->distance_index & 3U] =
                    (int32_t)distance;
                ++decoder->distance_index;
                remaining -= copy_length;
            }
        }
    }
    success = true;

cleanup:
    xx_br_meta_free(&meta);
    return success;
}

static bool xx_br_decode_window(xx_br_decoder *decoder) {
    uint32_t first;
    uint32_t value;
    if (!xx_br_read_bits(&decoder->bits, 1U, &first)) return false;
    if (first == 0U) {
        decoder->window_bits = 16U;
    } else {
        if (!xx_br_read_bits(&decoder->bits, 3U, &value)) return false;
        if (value != 0U) {
            decoder->window_bits = 17U + (unsigned)value;
        } else {
            if (!xx_br_read_bits(&decoder->bits, 3U, &value)) return false;
            if (value == 1U) {
                uint32_t reserved;
                if (!xx_br_read_bits(&decoder->bits, 1U, &reserved) ||
                    reserved != 0U ||
                    !xx_br_read_bits(&decoder->bits, 6U, &value) ||
                    value < 10U || value > 30U) {
                    return false;
                }
                decoder->window_bits = (unsigned)value;
            } else if (value != 0U) {
                decoder->window_bits = 8U + (unsigned)value;
            } else {
                decoder->window_bits = 17U;
            }
        }
    }
    decoder->maximum_backward_distance =
        ((size_t)1U << decoder->window_bits) - 16U;
    return true;
}

static bool xx_br_read_meta_header(xx_br_decoder *decoder, bool *is_last,
                                   bool *is_empty, bool *is_metadata,
                                   bool *is_uncompressed,
                                   size_t *meta_length) {
    uint32_t value;
    unsigned nibbles;
    size_t length = 0U;
    *is_empty = false;
    *is_metadata = false;
    *is_uncompressed = false;
    *meta_length = 0U;
    if (!xx_br_read_bits(&decoder->bits, 1U, &value)) return false;
    *is_last = value != 0U;
    if (*is_last) {
        if (!xx_br_read_bits(&decoder->bits, 1U, &value)) return false;
        if (value != 0U) {
            *is_empty = true;
            return true;
        }
    }
    if (!xx_br_read_bits(&decoder->bits, 2U, &value)) return false;
    nibbles = (unsigned)value + 4U;
    if (nibbles == 7U) {
        unsigned byte_count;
        *is_metadata = true;
        if (!xx_br_read_bits(&decoder->bits, 1U, &value) || value != 0U ||
            !xx_br_read_bits(&decoder->bits, 2U, &value)) {
            return false;
        }
        byte_count = (unsigned)value;
        if (byte_count == 0U) {
            *meta_length = 0U;
            return true;
        }
        for (unsigned index = 0U; index < byte_count; ++index) {
            if (!xx_br_read_bits(&decoder->bits, 8U, &value)) return false;
            if (index + 1U == byte_count && byte_count > 1U && value == 0U) {
                return false;
            }
            length |= (size_t)value << (index * 8U);
        }
        *meta_length = length + 1U;
        return true;
    }
    for (unsigned index = 0U; index < nibbles; ++index) {
        if (!xx_br_read_bits(&decoder->bits, 4U, &value)) return false;
        if (index + 1U == nibbles && nibbles > 4U && value == 0U) {
            return false;
        }
        length |= (size_t)value << (index * 4U);
    }
    if (!*is_last) {
        if (!xx_br_read_bits(&decoder->bits, 1U, &value)) return false;
        *is_uncompressed = value != 0U;
    }
    *meta_length = length + 1U;
    return true;
}

static bool xx_br_decode_raw(const uint8_t *source, size_t source_size,
                             uint8_t *destination, size_t destination_capacity,
                             size_t *out_written) {
    xx_br_decoder decoder;
    bool finished = false;

    if (out_written) *out_written = 0U;
    if (!source || source_size == 0U || !destination || !out_written ||
        source_size > SIZE_MAX / 8U) {
        return false;
    }
    decoder.bits.data = source;
    decoder.bits.bit_count = source_size * 8U;
    decoder.bits.position = 0U;
    decoder.output = destination;
    decoder.output_capacity = destination_capacity;
    decoder.output_position = 0U;
    decoder.grow_output = false;
    decoder.maximum_output_capacity = destination_capacity;
    decoder.window_bits = 0U;
    decoder.maximum_backward_distance = 0U;
    decoder.distances[0] = 16;
    decoder.distances[1] = 15;
    decoder.distances[2] = 11;
    decoder.distances[3] = 4;
    decoder.distance_index = 0;

    if (!xx_br_decode_window(&decoder)) return false;
    while (!finished) {
        bool is_last;
        bool is_empty;
        bool is_metadata;
        bool is_uncompressed;
        size_t meta_length;
        if (!xx_br_read_meta_header(&decoder, &is_last, &is_empty,
                                    &is_metadata, &is_uncompressed,
                                    &meta_length)) {
            return false;
        }
        if (is_empty) {
            finished = true;
            break;
        }
        if (is_metadata || is_uncompressed) {
            size_t byte_position;
            if (!xx_br_align_to_byte(&decoder.bits)) return false;
            byte_position = decoder.bits.position >> 3U;
            if (meta_length > source_size - byte_position) return false;
            if (is_metadata) {
                decoder.bits.position += meta_length * 8U;
            } else {
                if (!xx_br_reserve_output(&decoder, meta_length)) {
                    return false;
                }
                for (size_t index = 0U; index < meta_length; ++index) {
                    decoder.output[decoder.output_position + index] =
                        source[byte_position + index];
                }
                decoder.output_position += meta_length;
                decoder.bits.position += meta_length * 8U;
            }
        } else if (!xx_br_decode_compressed_meta(&decoder, meta_length)) {
            return false;
        }
        if (is_last) finished = true;
    }
    if (!xx_br_align_to_byte(&decoder.bits) ||
        decoder.bits.position != decoder.bits.bit_count) {
        return false;
    }
    *out_written = decoder.output_position;
    return true;
}

/* A growing variant for raw streams.  Brotli's normal wire format does not
 * publish an expanded size, so format readers need this bounded allocation
 * path instead of guessing a destination length. */
static bool xx_br_decode_raw_alloc(const uint8_t *source, size_t source_size,
                                   size_t maximum_output,
                                   uint8_t **out_data, size_t *out_written) {
    xx_br_decoder decoder;
    bool finished = false;
    size_t initial_capacity;

    if (out_data) *out_data = NULL;
    if (out_written) *out_written = 0U;
    if (!source || source_size == 0U || !out_data || !out_written ||
        source_size > SIZE_MAX / 8U) {
        return false;
    }
    initial_capacity = maximum_output < 64U ? maximum_output : 64U;
    if (initial_capacity == 0U) initial_capacity = 1U;
    xx_mem_zero(&decoder, sizeof(decoder));
    decoder.output = (uint8_t *)xx_mem_alloc(initial_capacity);
    if (!decoder.output) return false;
    decoder.bits.data = source;
    decoder.bits.bit_count = source_size * 8U;
    decoder.bits.position = 0U;
    decoder.output_capacity = initial_capacity;
    decoder.output_position = 0U;
    decoder.grow_output = true;
    decoder.maximum_output_capacity = maximum_output;
    decoder.window_bits = 0U;
    decoder.maximum_backward_distance = 0U;
    decoder.distances[0] = 16;
    decoder.distances[1] = 15;
    decoder.distances[2] = 11;
    decoder.distances[3] = 4;
    decoder.distance_index = 0;

    if (!xx_br_decode_window(&decoder)) goto error;
    while (!finished) {
        bool is_last;
        bool is_empty;
        bool is_metadata;
        bool is_uncompressed;
        size_t meta_length;
        if (!xx_br_read_meta_header(&decoder, &is_last, &is_empty,
                                    &is_metadata, &is_uncompressed,
                                    &meta_length)) {
            goto error;
        }
        if (is_empty) {
            finished = true;
            break;
        }
        if (is_metadata || is_uncompressed) {
            size_t byte_position;
            if (!xx_br_align_to_byte(&decoder.bits)) goto error;
            byte_position = decoder.bits.position >> 3U;
            if (meta_length > source_size - byte_position) goto error;
            if (is_metadata) {
                decoder.bits.position += meta_length * 8U;
            } else {
                size_t index;
                if (!xx_br_reserve_output(&decoder, meta_length)) goto error;
                for (index = 0U; index < meta_length; ++index) {
                    decoder.output[decoder.output_position + index] =
                        source[byte_position + index];
                }
                decoder.output_position += meta_length;
                decoder.bits.position += meta_length * 8U;
            }
        } else if (!xx_br_decode_compressed_meta(&decoder, meta_length)) {
            goto error;
        }
        if (is_last) finished = true;
    }
    if (!xx_br_align_to_byte(&decoder.bits) ||
        decoder.bits.position != decoder.bits.bit_count) {
        goto error;
    }
    *out_data = decoder.output;
    *out_written = decoder.output_position;
    return true;

error:
    xx_mem_free(decoder.output);
    return false;
}

static uint16_t xx_br_read16(const uint8_t *data) {
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8U);
}

static uint32_t xx_br_read32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool xx_br_is_mt_header(const uint8_t *source, size_t source_size) {
    return source_size >= 16U &&
           xx_br_read32(source) == UINT32_C(0x184D2A50) &&
           xx_br_read32(source + 4U) == UINT32_C(8) &&
           xx_br_read16(source + 12U) == UINT16_C(0x5242);
}

bool xx_brotli_decompress_memory(const void *source, size_t source_size,
                                 void *destination, size_t destination_size,
                                 size_t *out_written) {
    const uint8_t *input = (const uint8_t *)source;
    uint8_t *output = (uint8_t *)destination;
    uint8_t empty_output = 0U;
    size_t raw_written = 0U;
    size_t input_position = 0U;
    size_t output_position = 0U;
    bool saw_frame = false;

    if (out_written) *out_written = 0U;
    if (!out_written || (!input && source_size != 0U) ||
        (!output && destination_size != 0U) || source_size == 0U) {
        return false;
    }
    if (!output) output = &empty_output;
    if (xx_br_decode_raw(input, source_size, output, destination_size,
                         &raw_written) && raw_written == destination_size) {
        *out_written = raw_written;
        return true;
    }
    if (!xx_br_is_mt_header(input, source_size)) return false;

    while (input_position < source_size) {
        const uint8_t *header = input + input_position;
        uint32_t compressed_size;
        size_t frame_limit;
        size_t frame_capacity;
        size_t frame_written = 0U;
        if (!xx_br_is_mt_header(header, source_size - input_position)) {
            return false;
        }
        compressed_size = xx_br_read32(header + 8U);
        frame_limit = (size_t)xx_br_read16(header + 14U) << 16U;
        input_position += 16U;
        if ((size_t)compressed_size > source_size - input_position ||
            output_position > destination_size) {
            return false;
        }
        frame_capacity = destination_size - output_position;
        if (frame_capacity > frame_limit) frame_capacity = frame_limit;
        if (!xx_br_decode_raw(input + input_position, compressed_size,
                              output + output_position, frame_capacity,
                              &frame_written) ||
            frame_written > frame_limit ||
            frame_written > destination_size - output_position) {
            return false;
        }
        input_position += compressed_size;
        output_position += frame_written;
        saw_frame = true;
    }
    if (!saw_frame || output_position != destination_size) return false;
    *out_written = output_position;
    return true;
}

static bool xx_br_append_output(uint8_t **data, size_t *size,
                                size_t *capacity, const uint8_t *source,
                                size_t source_size) {
    size_t required;
    size_t next_capacity;
    uint8_t *grown;
    size_t index;
    if (!data || !size || !capacity || (!source && source_size != 0U) ||
        source_size > XX_BR_MAX_ALLOCATED_OUTPUT - *size) {
        return false;
    }
    required = *size + source_size;
    if (required > *capacity) {
        next_capacity = *capacity == 0U ? 64U : *capacity;
        while (next_capacity < required) {
            if (next_capacity > XX_BR_MAX_ALLOCATED_OUTPUT / 2U) {
                next_capacity = XX_BR_MAX_ALLOCATED_OUTPUT;
            } else {
                next_capacity *= 2U;
            }
            if (next_capacity < required &&
                next_capacity == XX_BR_MAX_ALLOCATED_OUTPUT) {
                return false;
            }
        }
        grown = (uint8_t *)xx_mem_realloc(*data, next_capacity);
        if (!grown) return false;
        *data = grown;
        *capacity = next_capacity;
    }
    for (index = 0U; index < source_size; ++index) {
        (*data)[*size + index] = source[index];
    }
    *size = required;
    return true;
}

bool xx_brotli_decompress_alloc(const void *source, size_t source_size,
                                uint8_t **out_data, size_t *out_size) {
    const uint8_t *input = (const uint8_t *)source;
    uint8_t *output = NULL;
    size_t output_size = 0U;
    size_t output_capacity = 0U;
    size_t input_position = 0U;
    bool saw_frame = false;

    if (out_data) *out_data = NULL;
    if (out_size) *out_size = 0U;
    if (!input || source_size == 0U || !out_data || !out_size) return false;
    if (!xx_br_is_mt_header(input, source_size)) {
        return xx_br_decode_raw_alloc(input, source_size,
                                      XX_BR_MAX_ALLOCATED_OUTPUT,
                                      out_data, out_size);
    }
    while (input_position < source_size) {
        const uint8_t *header;
        uint32_t compressed_size;
        size_t frame_limit;
        uint8_t *frame_data = NULL;
        size_t frame_size = 0U;
        size_t maximum_frame;
        if (source_size - input_position < 16U ||
            !xx_br_is_mt_header(input + input_position,
                                source_size - input_position)) {
            goto error;
        }
        header = input + input_position;
        compressed_size = xx_br_read32(header + 8U);
        frame_limit = (size_t)xx_br_read16(header + 14U) << 16U;
        input_position += 16U;
        if (frame_limit == 0U ||
            (size_t)compressed_size > source_size - input_position ||
            output_size > XX_BR_MAX_ALLOCATED_OUTPUT) {
            goto error;
        }
        maximum_frame = XX_BR_MAX_ALLOCATED_OUTPUT - output_size;
        if (frame_limit < maximum_frame) maximum_frame = frame_limit;
        if (!xx_br_decode_raw_alloc(input + input_position,
                                    (size_t)compressed_size, maximum_frame,
                                    &frame_data, &frame_size) ||
            !xx_br_append_output(&output, &output_size, &output_capacity,
                                 frame_data, frame_size)) {
            xx_mem_free(frame_data);
            goto error;
        }
        xx_mem_free(frame_data);
        input_position += (size_t)compressed_size;
        saw_frame = true;
    }
    if (!saw_frame) goto error;
    if (!output) {
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) goto error;
    }
    *out_data = output;
    *out_size = output_size;
    return true;

error:
    xx_mem_free(output);
    return false;
}
