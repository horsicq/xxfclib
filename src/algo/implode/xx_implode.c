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

/* Independent C11 implementation of PKWARE ZIP Implode (method 6). */

#include "xxfclib/algo/implode/xx_implode.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdint.h>

#define XX_IMPLODE_IO_BUFFER       4096u
#define XX_IMPLODE_HISTORY_SIZE    8192u
#define XX_IMPLODE_MAX_BITS        16u
#define XX_IMPLODE_MAX_SYMBOLS     256u
#define XX_IMPLODE_MAX_NODES       (XX_IMPLODE_MAX_SYMBOLS * 2u)
#define XX_IMPLODE_NO_NODE         (-1)

typedef struct xx_implode_node_s {
    int16_t child[2];
    int16_t symbol;
} xx_implode_node;

typedef struct xx_implode_tree_s {
    xx_implode_node nodes[XX_IMPLODE_MAX_NODES];
    unsigned node_count;
} xx_implode_tree;

typedef struct xx_implode_context_s {
    xx_io_device *src;
    xx_io_device *dst;
    xx_pd_struct *pd;
    int pd_level;

    bool use_8k_dictionary;
    bool use_literal_tree;
    int64_t input_left;
    int64_t expected_size;
    int64_t produced;

    uint8_t input[XX_IMPLODE_IO_BUFFER];
    size_t input_pos;
    size_t input_size;
    uint64_t bit_buffer;
    unsigned bit_count;

    uint8_t output[XX_IMPLODE_IO_BUFFER];
    size_t output_size;
    uint8_t history[XX_IMPLODE_HISTORY_SIZE];

    xx_implode_tree literal_tree;
    xx_implode_tree length_tree;
    xx_implode_tree distance_tree;
} xx_implode_context;

static void xx_implode_set_error(xx_implode_context *ctx, int code,
                                 const char *message) {
    if (ctx->pd && ctx->pd->last_error == 0) {
        xx_pd_set_error(ctx->pd, code, message);
    }
}

static bool xx_implode_cancelled(xx_implode_context *ctx) {
    if (ctx->pd && xx_pd_is_stopped(ctx->pd)) {
        xx_implode_set_error(ctx, 1, "Implode decompression cancelled");
        return true;
    }
    return false;
}

static bool xx_implode_flush(xx_implode_context *ctx) {
    if (ctx->output_size == 0) {
        return true;
    }
    if (xx_io_write(ctx->dst, ctx->output, ctx->output_size) !=
        (ssize_t)ctx->output_size) {
        xx_implode_set_error(ctx, 2, "Cannot write Implode output");
        return false;
    }
    ctx->output_size = 0;
    if (ctx->pd && ctx->pd_level >= 0) {
        xx_pd_set_current(ctx->pd, ctx->pd_level, (uint64_t)ctx->produced);
    }
    return true;
}

static bool xx_implode_emit(xx_implode_context *ctx, uint8_t value) {
    if (ctx->produced >= ctx->expected_size) {
        xx_implode_set_error(ctx, 3, "Implode output exceeds expected size");
        return false;
    }
    ctx->history[(size_t)ctx->produced & (XX_IMPLODE_HISTORY_SIZE - 1u)] = value;
    ctx->output[ctx->output_size++] = value;
    ++ctx->produced;
    if (ctx->output_size == sizeof(ctx->output)) {
        return xx_implode_flush(ctx) && !xx_implode_cancelled(ctx);
    }
    return true;
}

static bool xx_implode_read_byte(xx_implode_context *ctx, uint8_t *value) {
    if (ctx->input_pos == ctx->input_size) {
        size_t request;
        ssize_t count;

        if (ctx->input_left <= 0) {
            xx_implode_set_error(ctx, 4, "Truncated Implode stream");
            return false;
        }
        request = (ctx->input_left > (int64_t)sizeof(ctx->input))
                      ? sizeof(ctx->input)
                      : (size_t)ctx->input_left;
        count = xx_io_read(ctx->src, ctx->input, request);
        if (count <= 0 || (size_t)count != request) {
            xx_implode_set_error(ctx, 4, "Cannot read Implode stream");
            return false;
        }
        ctx->input_left -= count;
        ctx->input_pos = 0;
        ctx->input_size = (size_t)count;
    }
    *value = ctx->input[ctx->input_pos++];
    return true;
}

static bool xx_implode_read_bits(xx_implode_context *ctx, unsigned count,
                                 uint16_t *value) {
    uint8_t byte_value;
    uint64_t mask;

    while (ctx->bit_count < count) {
        if (!xx_implode_read_byte(ctx, &byte_value)) {
            return false;
        }
        ctx->bit_buffer |= ((uint64_t)byte_value) << ctx->bit_count;
        ctx->bit_count += 8;
    }
    mask = (((uint64_t)1u) << count) - 1u;
    *value = (uint16_t)(ctx->bit_buffer & mask);
    ctx->bit_buffer >>= count;
    ctx->bit_count -= count;
    return true;
}

static int xx_implode_new_node(xx_implode_tree *tree) {
    xx_implode_node *node;
    unsigned index;

    if (tree->node_count >= XX_IMPLODE_MAX_NODES) {
        return XX_IMPLODE_NO_NODE;
    }
    index = tree->node_count++;
    node = &tree->nodes[index];
    node->child[0] = XX_IMPLODE_NO_NODE;
    node->child[1] = XX_IMPLODE_NO_NODE;
    node->symbol = -1;
    return (int)index;
}

static bool xx_implode_insert_code(xx_implode_tree *tree,
                                   uint32_t code,
                                   unsigned length,
                                   unsigned symbol) {
    int node_index = 0;
    unsigned position;

    for (position = 0; position < length; ++position) {
        unsigned bit = (code >> (length - position - 1u)) & 1u;
        xx_implode_node *node = &tree->nodes[node_index];
        int next;

        if (node->symbol >= 0) {
            return false;
        }
        next = node->child[bit];
        if (next == XX_IMPLODE_NO_NODE) {
            next = xx_implode_new_node(tree);
            if (next == XX_IMPLODE_NO_NODE) {
                return false;
            }
            node = &tree->nodes[node_index];
            node->child[bit] = (int16_t)next;
        }
        node_index = next;
    }

    if (tree->nodes[node_index].symbol >= 0 ||
        tree->nodes[node_index].child[0] != XX_IMPLODE_NO_NODE ||
        tree->nodes[node_index].child[1] != XX_IMPLODE_NO_NODE) {
        return false;
    }
    tree->nodes[node_index].symbol = (int16_t)symbol;
    return true;
}

static bool xx_implode_build_tree(xx_implode_tree *tree,
                                  const uint8_t *lengths,
                                  unsigned symbol_count) {
    unsigned counts[XX_IMPLODE_MAX_BITS + 1u];
    uint32_t next_code[XX_IMPLODE_MAX_BITS + 1u];
    uint32_t code = 0;
    int left = 1;
    unsigned bits;
    unsigned symbol;

    xx_mem_zero(counts, sizeof(counts));
    xx_mem_zero(next_code, sizeof(next_code));
    tree->node_count = 0;
    if (xx_implode_new_node(tree) == XX_IMPLODE_NO_NODE) {
        return false;
    }

    for (symbol = 0; symbol < symbol_count; ++symbol) {
        if (lengths[symbol] == 0 || lengths[symbol] > XX_IMPLODE_MAX_BITS) {
            return false;
        }
        ++counts[lengths[symbol]];
    }
    for (bits = 1; bits <= XX_IMPLODE_MAX_BITS; ++bits) {
        left = (left << 1) - (int)counts[bits];
        if (left < 0) {
            return false;
        }
    }
    if (left != 0) {
        return false;
    }

    for (bits = 1; bits <= XX_IMPLODE_MAX_BITS; ++bits) {
        code = (code + counts[bits - 1u]) << 1;
        next_code[bits] = code;
    }

    for (symbol = 0; symbol < symbol_count; ++symbol) {
        unsigned length = lengths[symbol];
        uint32_t mask = (((uint32_t)1u) << length) - 1u;
        uint32_t complemented = (~next_code[length]++) & mask;
        if (!xx_implode_insert_code(tree, complemented, length, symbol)) {
            return false;
        }
    }
    return true;
}

static bool xx_implode_read_tree(xx_implode_context *ctx,
                                 xx_implode_tree *tree,
                                 unsigned symbol_count) {
    uint8_t lengths[XX_IMPLODE_MAX_SYMBOLS];
    uint16_t value;
    unsigned pair_count;
    unsigned filled = 0;
    unsigned pair;

    if (symbol_count > sizeof(lengths) ||
        !xx_implode_read_bits(ctx, 8, &value)) {
        return false;
    }
    pair_count = value + 1u;
    for (pair = 0; pair < pair_count; ++pair) {
        unsigned run;
        unsigned length;
        unsigned index;

        if (!xx_implode_read_bits(ctx, 8, &value)) {
            return false;
        }
        run = (value >> 4) + 1u;
        length = (value & 15u) + 1u;
        if (run > symbol_count - filled) {
            xx_implode_set_error(ctx, 5, "Invalid Implode code-length tree");
            return false;
        }
        for (index = 0; index < run; ++index) {
            lengths[filled++] = (uint8_t)length;
        }
    }
    if (filled != symbol_count ||
        !xx_implode_build_tree(tree, lengths, symbol_count)) {
        xx_implode_set_error(ctx, 5, "Invalid Implode Huffman tree");
        return false;
    }
    return true;
}

static bool xx_implode_decode_symbol(xx_implode_context *ctx,
                                     const xx_implode_tree *tree,
                                     uint16_t *symbol) {
    int node_index = 0;
    unsigned depth;

    for (depth = 0; depth <= XX_IMPLODE_MAX_BITS; ++depth) {
        const xx_implode_node *node = &tree->nodes[node_index];
        uint16_t bit;

        if (node->symbol >= 0) {
            *symbol = (uint16_t)node->symbol;
            return true;
        }
        if (depth == XX_IMPLODE_MAX_BITS ||
            !xx_implode_read_bits(ctx, 1, &bit)) {
            return false;
        }
        node_index = node->child[bit];
        if (node_index == XX_IMPLODE_NO_NODE) {
            xx_implode_set_error(ctx, 5, "Invalid Implode Huffman code");
            return false;
        }
    }
    return false;
}

static bool xx_implode_copy_match(xx_implode_context *ctx,
                                  unsigned distance,
                                  unsigned length) {
    unsigned index;
    unsigned dictionary_size = ctx->use_8k_dictionary ? 8192u : 4096u;

    if (distance == 0 || distance > dictionary_size ||
        (int64_t)length > ctx->expected_size - ctx->produced) {
        xx_implode_set_error(ctx, 5, "Invalid Implode back-reference");
        return false;
    }
    for (index = 0; index < length; ++index) {
        uint8_t value = 0;
        if ((int64_t)distance <= ctx->produced) {
            value = ctx->history[((size_t)ctx->produced - distance) &
                                 (XX_IMPLODE_HISTORY_SIZE - 1u)];
        }
        if (!xx_implode_emit(ctx, value)) {
            return false;
        }
    }
    return true;
}

static bool xx_implode_decode(xx_implode_context *ctx) {
    unsigned low_distance_bits = ctx->use_8k_dictionary ? 7u : 6u;
    unsigned minimum_length = ctx->use_literal_tree ? 3u : 2u;

    if (ctx->use_literal_tree &&
        !xx_implode_read_tree(ctx, &ctx->literal_tree, 256)) {
        return false;
    }
    if (!xx_implode_read_tree(ctx, &ctx->length_tree, 64) ||
        !xx_implode_read_tree(ctx, &ctx->distance_tree, 64)) {
        return false;
    }

    while (ctx->produced < ctx->expected_size) {
        uint16_t token;

        if (xx_implode_cancelled(ctx) ||
            !xx_implode_read_bits(ctx, 1, &token)) {
            return false;
        }
        if (token != 0) {
            uint16_t literal;
            if (ctx->use_literal_tree) {
                if (!xx_implode_decode_symbol(ctx, &ctx->literal_tree,
                                              &literal)) {
                    return false;
                }
            } else if (!xx_implode_read_bits(ctx, 8, &literal)) {
                return false;
            }
            if (!xx_implode_emit(ctx, (uint8_t)literal)) {
                return false;
            }
        } else {
            uint16_t distance_low;
            uint16_t distance_high;
            uint16_t length_symbol;
            unsigned length;
            unsigned distance;

            if (!xx_implode_read_bits(ctx, low_distance_bits, &distance_low) ||
                !xx_implode_decode_symbol(ctx, &ctx->distance_tree,
                                          &distance_high) ||
                !xx_implode_decode_symbol(ctx, &ctx->length_tree,
                                          &length_symbol)) {
                return false;
            }
            distance = 1u + distance_low +
                       ((unsigned)distance_high << low_distance_bits);
            length = (unsigned)length_symbol + minimum_length;
            if (length_symbol == 63u) {
                uint16_t extra;
                if (!xx_implode_read_bits(ctx, 8, &extra)) {
                    return false;
                }
                length += extra;
            }
            if (!xx_implode_copy_match(ctx, distance, length)) {
                return false;
            }
        }
    }

    return xx_implode_flush(ctx);
}

bool xx_implode_unpack_device(xx_io_device *src_dev, int64_t src_offset,
                              int64_t comp_size, xx_io_device *dst_dev,
                              int64_t expected_size, bool use_8k_dictionary,
                              bool use_literal_tree, xx_pd_struct *pd) {
    xx_implode_context *ctx;
    bool result;

    if (!src_dev || !dst_dev || comp_size < 0 || expected_size < 0 ||
        src_offset < -1) {
        return false;
    }
    if (src_offset >= 0 && xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) {
        return false;
    }
    if (expected_size == 0) {
        return true;
    }

    ctx = (xx_implode_context *)xx_mem_alloc(sizeof(*ctx));
    if (!ctx) {
        if (pd) xx_pd_set_error(pd, 6, "Cannot allocate Implode decoder");
        return false;
    }
    xx_mem_zero(ctx, sizeof(*ctx));
    ctx->src = src_dev;
    ctx->dst = dst_dev;
    ctx->pd = pd;
    ctx->pd_level = -1;
    ctx->use_8k_dictionary = use_8k_dictionary;
    ctx->use_literal_tree = use_literal_tree;
    ctx->input_left = comp_size;
    ctx->expected_size = expected_size;
    if (pd) {
        ctx->pd_level = xx_pd_enter_level(pd, (uint64_t)expected_size,
                                          "Unpacking ZIP Implode");
    }

    result = xx_implode_decode(ctx) && ctx->produced == expected_size;
    if (!result && pd && pd->last_error == 0) {
        xx_pd_set_error(pd, 5, "Invalid Implode compressed data");
    }
    if (pd && ctx->pd_level >= 0) {
        xx_pd_leave_level(pd, ctx->pd_level);
    }
    xx_mem_free(ctx);
    return result;
}

bool xx_implode_unpack_device_to_file(xx_io_device *src_dev,
                                      int64_t src_offset,
                                      int64_t comp_size,
                                      const char *dst_file_path,
                                      int64_t expected_size,
                                      bool use_8k_dictionary,
                                      bool use_literal_tree,
                                      xx_pd_struct *pd) {
    xx_io_device *dst_dev;
    bool result;

    if (!src_dev || !dst_file_path) {
        return false;
    }
    dst_dev = xx_io_file_open(dst_file_path, "wb");
    if (!dst_dev) {
        return false;
    }
    result = xx_implode_unpack_device(src_dev, src_offset, comp_size, dst_dev,
                                      expected_size, use_8k_dictionary,
                                      use_literal_tree, pd);
    xx_io_close(dst_dev);
    return result;
}
