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

/* Independent C11 implementation of PKWARE ZIP Reduce (methods 2--5). */

#include "xxfclib/algo/reduce/xx_reduce.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdint.h>

#define XX_REDUCE_IO_BUFFER       4096u
#define XX_REDUCE_HISTORY_SIZE    4096u
#define XX_REDUCE_FOLLOWER_LIMIT  32u
#define XX_REDUCE_ESCAPE          0x90u

typedef struct xx_reduce_followers_s {
    uint8_t count;
    uint8_t bit_count;
    uint8_t value[XX_REDUCE_FOLLOWER_LIMIT];
} xx_reduce_followers;

typedef struct xx_reduce_context_s {
    xx_io_device *src;
    xx_io_device *dst;
    xx_pd_struct *pd;
    int pd_level;

    int factor;
    int64_t input_left;
    int64_t expected_size;
    int64_t produced;

    uint8_t input[XX_REDUCE_IO_BUFFER];
    size_t input_pos;
    size_t input_size;
    uint64_t bit_buffer;
    unsigned bit_count;

    uint8_t output[XX_REDUCE_IO_BUFFER];
    size_t output_size;
    uint8_t history[XX_REDUCE_HISTORY_SIZE];

    xx_reduce_followers followers[256];
    uint8_t previous_encoded;
} xx_reduce_context;

static void xx_reduce_set_error(xx_reduce_context *ctx, int code,
                                const char *message) {
    if (ctx->pd && ctx->pd->last_error == 0) {
        xx_pd_set_error(ctx->pd, code, message);
    }
}

static bool xx_reduce_cancelled(xx_reduce_context *ctx) {
    if (ctx->pd && xx_pd_is_stopped(ctx->pd)) {
        xx_reduce_set_error(ctx, 1, "Reduce decompression cancelled");
        return true;
    }
    return false;
}

static bool xx_reduce_flush(xx_reduce_context *ctx) {
    if (ctx->output_size == 0) {
        return true;
    }
    if (xx_io_write(ctx->dst, ctx->output, ctx->output_size) !=
        (ssize_t)ctx->output_size) {
        xx_reduce_set_error(ctx, 2, "Cannot write Reduce output");
        return false;
    }
    ctx->output_size = 0;
    if (ctx->pd && ctx->pd_level >= 0) {
        xx_pd_set_current(ctx->pd, ctx->pd_level, (uint64_t)ctx->produced);
    }
    return true;
}

static bool xx_reduce_emit(xx_reduce_context *ctx, uint8_t value) {
    if (ctx->produced >= ctx->expected_size) {
        xx_reduce_set_error(ctx, 3, "Reduce output exceeds expected size");
        return false;
    }
    ctx->history[(size_t)ctx->produced & (XX_REDUCE_HISTORY_SIZE - 1u)] = value;
    ctx->output[ctx->output_size++] = value;
    ++ctx->produced;
    if (ctx->output_size == sizeof(ctx->output)) {
        return xx_reduce_flush(ctx) && !xx_reduce_cancelled(ctx);
    }
    return true;
}

static bool xx_reduce_read_byte(xx_reduce_context *ctx, uint8_t *value) {
    if (ctx->input_pos == ctx->input_size) {
        size_t request;
        ssize_t count;

        if (ctx->input_left <= 0) {
            xx_reduce_set_error(ctx, 4, "Truncated Reduce stream");
            return false;
        }
        request = (ctx->input_left > (int64_t)sizeof(ctx->input))
                      ? sizeof(ctx->input)
                      : (size_t)ctx->input_left;
        count = xx_io_read(ctx->src, ctx->input, request);
        if (count <= 0 || (size_t)count != request) {
            xx_reduce_set_error(ctx, 4, "Cannot read Reduce stream");
            return false;
        }
        ctx->input_left -= count;
        ctx->input_pos = 0;
        ctx->input_size = (size_t)count;
    }
    *value = ctx->input[ctx->input_pos++];
    return true;
}

static bool xx_reduce_read_bits(xx_reduce_context *ctx, unsigned count,
                                uint16_t *value) {
    uint8_t byte_value;
    uint64_t mask;

    while (ctx->bit_count < count) {
        if (!xx_reduce_read_byte(ctx, &byte_value)) {
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

static unsigned xx_reduce_follower_bits(unsigned count) {
    unsigned bits = 1;
    unsigned maximum = 2;

    if (count == 0) {
        return 0;
    }
    while (maximum < count) {
        maximum <<= 1;
        ++bits;
    }
    return bits;
}

static bool xx_reduce_read_follower_sets(xx_reduce_context *ctx) {
    int symbol;

    for (symbol = 255; symbol >= 0; --symbol) {
        xx_reduce_followers *set = &ctx->followers[symbol];
        uint16_t count;
        unsigned index;

        if (!xx_reduce_read_bits(ctx, 6, &count) ||
            count > XX_REDUCE_FOLLOWER_LIMIT) {
            xx_reduce_set_error(ctx, 5, "Invalid Reduce follower set");
            return false;
        }
        set->count = (uint8_t)count;
        set->bit_count = (uint8_t)xx_reduce_follower_bits(count);
        for (index = 0; index < count; ++index) {
            uint16_t value;
            if (!xx_reduce_read_bits(ctx, 8, &value)) {
                return false;
            }
            set->value[index] = (uint8_t)value;
        }
    }
    return true;
}

static bool xx_reduce_read_encoded_byte(xx_reduce_context *ctx,
                                        uint8_t *value) {
    const xx_reduce_followers *set = &ctx->followers[ctx->previous_encoded];
    uint16_t raw;

    if (set->count != 0) {
        uint16_t selector;
        if (!xx_reduce_read_bits(ctx, 1, &selector)) {
            return false;
        }
        if (selector == 0) {
            uint16_t index;
            if (!xx_reduce_read_bits(ctx, set->bit_count, &index) ||
                index >= set->count) {
                xx_reduce_set_error(ctx, 5, "Invalid Reduce follower index");
                return false;
            }
            *value = set->value[index];
        } else {
            if (!xx_reduce_read_bits(ctx, 8, &raw)) {
                return false;
            }
            *value = (uint8_t)raw;
        }
    } else {
        if (!xx_reduce_read_bits(ctx, 8, &raw)) {
            return false;
        }
        *value = (uint8_t)raw;
    }

    ctx->previous_encoded = *value;
    return true;
}

static bool xx_reduce_copy_match(xx_reduce_context *ctx,
                                 unsigned distance,
                                 unsigned length) {
    unsigned index;

    if (distance == 0 || distance > XX_REDUCE_HISTORY_SIZE ||
        (int64_t)length > ctx->expected_size - ctx->produced) {
        xx_reduce_set_error(ctx, 5, "Invalid Reduce back-reference");
        return false;
    }
    for (index = 0; index < length; ++index) {
        uint8_t value = 0;
        if ((int64_t)distance <= ctx->produced) {
            value = ctx->history[((size_t)ctx->produced - distance) &
                                 (XX_REDUCE_HISTORY_SIZE - 1u)];
        }
        if (!xx_reduce_emit(ctx, value)) {
            return false;
        }
    }
    return true;
}

static bool xx_reduce_decode(xx_reduce_context *ctx) {
    unsigned length_mask = (1u << (8 - ctx->factor)) - 1u;

    if (!xx_reduce_read_follower_sets(ctx)) {
        return false;
    }

    while (ctx->produced < ctx->expected_size) {
        uint8_t value;

        if (xx_reduce_cancelled(ctx) ||
            !xx_reduce_read_encoded_byte(ctx, &value)) {
            return false;
        }
        if (value != XX_REDUCE_ESCAPE) {
            if (!xx_reduce_emit(ctx, value)) {
                return false;
            }
            continue;
        }

        if (!xx_reduce_read_encoded_byte(ctx, &value)) {
            return false;
        }
        if (value == 0) {
            if (!xx_reduce_emit(ctx, XX_REDUCE_ESCAPE)) {
                return false;
            }
        } else {
            unsigned length = value & length_mask;
            unsigned distance_high = value >> (8 - ctx->factor);
            uint8_t low;

            if (length == length_mask) {
                uint8_t extra;
                if (!xx_reduce_read_encoded_byte(ctx, &extra)) {
                    return false;
                }
                length += extra;
            }
            if (!xx_reduce_read_encoded_byte(ctx, &low)) {
                return false;
            }
            if (!xx_reduce_copy_match(ctx, (distance_high << 8) + low + 1u,
                                      length + 3u)) {
                return false;
            }
        }
    }

    return xx_reduce_flush(ctx);
}

bool xx_reduce_unpack_device(xx_io_device *src_dev, int64_t src_offset,
                             int64_t comp_size, xx_io_device *dst_dev,
                             int64_t expected_size, int factor,
                             xx_pd_struct *pd) {
    xx_reduce_context *ctx;
    bool result;

    if (!src_dev || !dst_dev || comp_size < 0 || expected_size < 0 ||
        factor < 1 || factor > 4 || src_offset < -1) {
        return false;
    }
    if (src_offset >= 0 && xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) {
        return false;
    }
    if (expected_size == 0) {
        return true;
    }

    ctx = (xx_reduce_context *)xx_mem_alloc(sizeof(*ctx));
    if (!ctx) {
        if (pd) xx_pd_set_error(pd, 6, "Cannot allocate Reduce decoder");
        return false;
    }
    xx_mem_zero(ctx, sizeof(*ctx));
    ctx->src = src_dev;
    ctx->dst = dst_dev;
    ctx->pd = pd;
    ctx->pd_level = -1;
    ctx->factor = factor;
    ctx->input_left = comp_size;
    ctx->expected_size = expected_size;
    if (pd) {
        ctx->pd_level = xx_pd_enter_level(pd, (uint64_t)expected_size,
                                          "Unpacking ZIP Reduce");
    }

    result = xx_reduce_decode(ctx) && ctx->produced == expected_size;
    if (!result && pd && pd->last_error == 0) {
        xx_pd_set_error(pd, 5, "Invalid Reduce compressed data");
    }
    if (pd && ctx->pd_level >= 0) {
        xx_pd_leave_level(pd, ctx->pd_level);
    }
    xx_mem_free(ctx);
    return result;
}

bool xx_reduce_unpack_device_to_file(xx_io_device *src_dev,
                                     int64_t src_offset,
                                     int64_t comp_size,
                                     const char *dst_file_path,
                                     int64_t expected_size,
                                     int factor,
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
    result = xx_reduce_unpack_device(src_dev, src_offset, comp_size, dst_dev,
                                     expected_size, factor, pd);
    xx_io_close(dst_dev);
    if (!result) {
        xx_io_file_remove_a(dst_file_path);
    }
    return result;
}
