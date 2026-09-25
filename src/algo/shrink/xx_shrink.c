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

/* Independent C11 implementation of PKWARE ZIP Shrink (method 1). */

#include "xxfclib/algo/shrink/xx_shrink.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdint.h>

#define XX_SHRINK_LITERAL_COUNT 256u
#define XX_SHRINK_CONTROL_CODE  256u
#define XX_SHRINK_FIRST_CODE    257u
#define XX_SHRINK_CODE_COUNT    8192u
#define XX_SHRINK_MIN_BITS      9u
#define XX_SHRINK_MAX_BITS      13u
#define XX_SHRINK_IO_BUFFER     4096u

typedef struct xx_shrink_entry_s {
    uint16_t prefix;
    uint8_t suffix;
    uint8_t used;
} xx_shrink_entry;

typedef struct xx_shrink_context_s {
    xx_io_device *src;
    xx_io_device *dst;
    xx_pd_struct *pd;
    int pd_level;

    int64_t input_left;
    int64_t expected_size;
    int64_t produced;

    uint8_t input[XX_SHRINK_IO_BUFFER];
    size_t input_pos;
    size_t input_size;
    uint64_t bit_buffer;
    unsigned bit_count;

    uint8_t output[XX_SHRINK_IO_BUFFER];
    size_t output_size;

    xx_shrink_entry dictionary[XX_SHRINK_CODE_COUNT];
    uint8_t available[XX_SHRINK_CODE_COUNT];
    uint8_t prefix_mark[XX_SHRINK_CODE_COUNT];
    uint8_t reverse[XX_SHRINK_CODE_COUNT];
    uint16_t next_available;
} xx_shrink_context;

static void xx_shrink_set_error(xx_shrink_context *ctx, int code,
                                const char *message) {
    if (ctx->pd && ctx->pd->last_error == 0) {
        xx_pd_set_error(ctx->pd, code, message);
    }
}

static bool xx_shrink_cancelled(xx_shrink_context *ctx) {
    if (ctx->pd && xx_pd_is_stopped(ctx->pd)) {
        xx_shrink_set_error(ctx, 1, "Shrink decompression cancelled");
        return true;
    }
    return false;
}

static bool xx_shrink_flush(xx_shrink_context *ctx) {
    if (ctx->output_size == 0) {
        return true;
    }
    if (xx_io_write(ctx->dst, ctx->output, ctx->output_size) !=
        (ssize_t)ctx->output_size) {
        xx_shrink_set_error(ctx, 2, "Cannot write Shrink output");
        return false;
    }
    ctx->output_size = 0;
    if (ctx->pd && ctx->pd_level >= 0) {
        xx_pd_set_current(ctx->pd, ctx->pd_level, (uint64_t)ctx->produced);
    }
    return true;
}

static bool xx_shrink_emit(xx_shrink_context *ctx, uint8_t value) {
    if (ctx->produced >= ctx->expected_size) {
        xx_shrink_set_error(ctx, 3, "Shrink output exceeds expected size");
        return false;
    }
    ctx->output[ctx->output_size++] = value;
    ctx->produced++;
    if (ctx->output_size == sizeof(ctx->output)) {
        return xx_shrink_flush(ctx) && !xx_shrink_cancelled(ctx);
    }
    return true;
}

static bool xx_shrink_read_byte(xx_shrink_context *ctx, uint8_t *value) {
    if (ctx->input_pos == ctx->input_size) {
        size_t request;
        ssize_t count;

        if (ctx->input_left <= 0) {
            xx_shrink_set_error(ctx, 4, "Truncated Shrink stream");
            return false;
        }
        request = (ctx->input_left > (int64_t)sizeof(ctx->input))
                      ? sizeof(ctx->input)
                      : (size_t)ctx->input_left;
        count = xx_io_read(ctx->src, ctx->input, request);
        if (count <= 0 || (size_t)count != request) {
            xx_shrink_set_error(ctx, 4, "Cannot read Shrink stream");
            return false;
        }
        ctx->input_left -= count;
        ctx->input_pos = 0;
        ctx->input_size = (size_t)count;
    }

    *value = ctx->input[ctx->input_pos++];
    return true;
}

static bool xx_shrink_read_bits(xx_shrink_context *ctx, unsigned count,
                                uint16_t *value) {
    uint8_t byte_value;
    uint64_t mask;

    while (ctx->bit_count < count) {
        if (!xx_shrink_read_byte(ctx, &byte_value)) {
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

static void xx_shrink_init_dictionary(xx_shrink_context *ctx) {
    uint16_t code;

    for (code = 0; code < XX_SHRINK_LITERAL_COUNT; ++code) {
        ctx->dictionary[code].used = 1;
        ctx->dictionary[code].suffix = (uint8_t)code;
    }
    for (code = XX_SHRINK_FIRST_CODE; code < XX_SHRINK_CODE_COUNT; ++code) {
        ctx->available[code] = 1;
    }
    ctx->next_available = XX_SHRINK_FIRST_CODE;
}

static uint16_t xx_shrink_peek_available(xx_shrink_context *ctx) {
    uint16_t code = ctx->next_available;

    while (code < XX_SHRINK_CODE_COUNT && !ctx->available[code]) {
        ++code;
    }
    ctx->next_available = code;
    return code;
}

static uint16_t xx_shrink_take_available(xx_shrink_context *ctx) {
    uint16_t code = xx_shrink_peek_available(ctx);
    if (code < XX_SHRINK_CODE_COUNT) {
        ctx->available[code] = 0;
        ctx->next_available = (uint16_t)(code + 1u);
    }
    return code;
}

static void xx_shrink_partial_clear(xx_shrink_context *ctx) {
    uint16_t code;

    xx_mem_zero(ctx->prefix_mark, sizeof(ctx->prefix_mark));
    for (code = XX_SHRINK_FIRST_CODE; code < XX_SHRINK_CODE_COUNT; ++code) {
        if (ctx->dictionary[code].used) {
            uint16_t prefix = ctx->dictionary[code].prefix;
            if (prefix >= XX_SHRINK_FIRST_CODE &&
                prefix < XX_SHRINK_CODE_COUNT) {
                ctx->prefix_mark[prefix] = 1;
            }
        }
    }

    for (code = XX_SHRINK_FIRST_CODE; code < XX_SHRINK_CODE_COUNT; ++code) {
        if (ctx->dictionary[code].used && !ctx->prefix_mark[code]) {
            ctx->dictionary[code].used = 0;
        }
        /* A free code that is still referenced is deliberately unavailable. */
        ctx->available[code] = ctx->prefix_mark[code] ? 0 : 1;
    }
    ctx->next_available = XX_SHRINK_FIRST_CODE;
}

static bool xx_shrink_define_pending_prefix(xx_shrink_context *ctx,
                                            uint16_t code,
                                            uint16_t previous_code,
                                            uint8_t previous_first) {
    uint16_t next = xx_shrink_peek_available(ctx);
    if (code != next || code >= XX_SHRINK_CODE_COUNT) {
        return false;
    }
    ctx->dictionary[code].used = 1;
    ctx->dictionary[code].prefix = previous_code;
    ctx->dictionary[code].suffix = previous_first;
    return true;
}

static bool xx_shrink_expand_code(xx_shrink_context *ctx, uint16_t code,
                                  uint16_t previous_code,
                                  uint8_t previous_first,
                                  uint8_t *first_byte) {
    size_t count = 0;
    uint16_t cursor = code;

    if (cursor == XX_SHRINK_CONTROL_CODE || cursor >= XX_SHRINK_CODE_COUNT) {
        xx_shrink_set_error(ctx, 5, "Invalid Shrink dictionary code");
        return false;
    }

    while (cursor >= XX_SHRINK_FIRST_CODE) {
        xx_shrink_entry *entry;

        if (cursor >= XX_SHRINK_CODE_COUNT || count >= sizeof(ctx->reverse)) {
            xx_shrink_set_error(ctx, 5, "Cyclic Shrink dictionary entry");
            return false;
        }
        if (!ctx->dictionary[cursor].used &&
            !xx_shrink_define_pending_prefix(ctx, cursor, previous_code,
                                             previous_first)) {
            xx_shrink_set_error(ctx, 5, "Undefined Shrink dictionary code");
            return false;
        }

        entry = &ctx->dictionary[cursor];
        if (entry->prefix == cursor) {
            xx_shrink_set_error(ctx, 5, "Self-referencing Shrink dictionary code");
            return false;
        }
        ctx->reverse[count++] = entry->suffix;
        cursor = entry->prefix;
    }

    if (cursor >= XX_SHRINK_LITERAL_COUNT) {
        xx_shrink_set_error(ctx, 5, "Invalid Shrink dictionary prefix");
        return false;
    }
    if ((int64_t)(count + 1u) > ctx->expected_size - ctx->produced) {
        xx_shrink_set_error(ctx, 3, "Shrink output exceeds expected size");
        return false;
    }

    *first_byte = (uint8_t)cursor;
    if (!xx_shrink_emit(ctx, (uint8_t)cursor)) {
        return false;
    }
    while (count > 0) {
        if (!xx_shrink_emit(ctx, ctx->reverse[--count])) {
            return false;
        }
    }
    return true;
}

static bool xx_shrink_decode(xx_shrink_context *ctx) {
    unsigned code_bits = XX_SHRINK_MIN_BITS;
    uint16_t previous_code = 0;
    uint8_t previous_first = 0;
    bool have_previous = false;

    xx_shrink_init_dictionary(ctx);

    while (ctx->produced < ctx->expected_size) {
        uint16_t code;
        uint8_t current_first;

        if (xx_shrink_cancelled(ctx) ||
            !xx_shrink_read_bits(ctx, code_bits, &code)) {
            return false;
        }

        if (code == XX_SHRINK_CONTROL_CODE) {
            uint16_t command;
            if (!xx_shrink_read_bits(ctx, code_bits, &command)) {
                return false;
            }
            if (command == 1u && code_bits < XX_SHRINK_MAX_BITS) {
                ++code_bits;
            } else if (command == 2u) {
                xx_shrink_partial_clear(ctx);
            } else {
                xx_shrink_set_error(ctx, 5, "Invalid Shrink control code");
                return false;
            }
            continue;
        }

        if (!have_previous) {
            if (code >= XX_SHRINK_LITERAL_COUNT) {
                xx_shrink_set_error(ctx, 5,
                                    "First Shrink code is not a literal");
                return false;
            }
            current_first = (uint8_t)code;
            if (!xx_shrink_emit(ctx, current_first)) {
                return false;
            }
            previous_code = code;
            previous_first = current_first;
            have_previous = true;
            continue;
        }

        if (!xx_shrink_expand_code(ctx, code, previous_code, previous_first,
                                   &current_first)) {
            return false;
        }

        if (ctx->produced < ctx->expected_size) {
            uint16_t new_code = xx_shrink_take_available(ctx);
            if (new_code < XX_SHRINK_CODE_COUNT) {
                ctx->dictionary[new_code].used = 1;
                ctx->dictionary[new_code].prefix = previous_code;
                ctx->dictionary[new_code].suffix = current_first;
            }
        }

        previous_code = code;
        previous_first = current_first;
    }

    return xx_shrink_flush(ctx);
}

bool xx_shrink_unpack_device(xx_io_device *src_dev, int64_t src_offset,
                             int64_t comp_size, xx_io_device *dst_dev,
                             int64_t expected_size, xx_pd_struct *pd) {
    xx_shrink_context *ctx;
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

    ctx = (xx_shrink_context *)xx_mem_alloc(sizeof(*ctx));
    if (!ctx) {
        if (pd) xx_pd_set_error(pd, 6, "Cannot allocate Shrink decoder");
        return false;
    }
    xx_mem_zero(ctx, sizeof(*ctx));
    ctx->src = src_dev;
    ctx->dst = dst_dev;
    ctx->pd = pd;
    ctx->pd_level = -1;
    ctx->input_left = comp_size;
    ctx->expected_size = expected_size;
    if (pd) {
        ctx->pd_level = xx_pd_enter_level(pd, (uint64_t)expected_size,
                                          "Unpacking ZIP Shrink");
    }

    result = xx_shrink_decode(ctx) && ctx->produced == expected_size;
    if (!result && pd && pd->last_error == 0) {
        xx_pd_set_error(pd, 5, "Invalid Shrink compressed data");
    }
    if (pd && ctx->pd_level >= 0) {
        xx_pd_leave_level(pd, ctx->pd_level);
    }
    xx_mem_free(ctx);
    return result;
}

bool xx_shrink_unpack_device_to_file(xx_io_device *src_dev,
                                     int64_t src_offset,
                                     int64_t comp_size,
                                     const char *dst_file_path,
                                     int64_t expected_size,
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
    result = xx_shrink_unpack_device(src_dev, src_offset, comp_size, dst_dev,
                                     expected_size, pd);
    xx_io_close(dst_dev);
    if (!result) {
        xx_io_file_remove_a(dst_file_path);
    }
    return result;
}
