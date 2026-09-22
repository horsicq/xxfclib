/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The ArcFS LZW bit stream is related to Unix compress, but its block
 * alignment is measured in *codes*, not absolute bits.  Keeping that state in
 * the reader avoids the usual desynchronisation when a stream clears or
 * changes width more than once.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/arcfs/xx_arcfs_lzw.h"

#include "xxfclib/memory/xx_memory.h"

#include <string.h>

#define ARCFS_LZW_CLEAR 0x100U
#define ARCFS_LZW_FIRST_FREE 0x101U

typedef struct arcfs_sink_s {
    uint8_t *output;
    size_t output_size;
    size_t position;
    bool rle90;
    bool escaped;
    uint8_t last;
} arcfs_sink;

typedef struct arcfs_lzw_reader_s {
    const uint8_t *input;
    size_t bit_size;
    size_t bit_position;
    uint32_t width;
    uint32_t max_code;
    uint32_t free_code;
    uint32_t capacity;
    uint32_t group_count;
    uint8_t max_bits;
} arcfs_lzw_reader;

static bool arcfs_sink_emit(arcfs_sink *sink, uint8_t value) {
    if (!sink || sink->position >= sink->output_size) return false;
    sink->output[sink->position++] = value;
    return true;
}

static bool arcfs_sink_push(arcfs_sink *sink, uint8_t value) {
    size_t repeat;
    if (!sink) return false;
    if (!sink->rle90) return arcfs_sink_emit(sink, value);
    if (!sink->escaped) {
        if (value == 0x90U) {
            sink->escaped = true;
            return true;
        }
        sink->last = value;
        return arcfs_sink_emit(sink, value);
    }
    sink->escaped = false;
    if (value == 0U) {
        /* An escaped 0x90 is literal and deliberately does not change last. */
        return arcfs_sink_emit(sink, 0x90U);
    }
    repeat = (size_t)value - 1U;
    if (repeat > sink->output_size - sink->position) return false;
    if (repeat != 0U) xx_rt_memset(sink->output + sink->position, sink->last, repeat);
    sink->position += repeat;
    return true;
}

static bool arcfs_reader_pad(arcfs_lzw_reader *reader) {
    size_t skip;
    if (!reader) return false;
    skip = (size_t)((8U - (reader->group_count & 7U)) & 7U) *
           (size_t)reader->width;
    if (skip > SIZE_MAX - reader->bit_position) return false;
    reader->bit_position += skip;
    reader->group_count = 0U;
    return reader->bit_position <= reader->bit_size;
}

static bool arcfs_reader_reset(arcfs_lzw_reader *reader) {
    if (!reader || !arcfs_reader_pad(reader)) return false;
    reader->width = 9U;
    reader->max_code = 0x1ffU;
    reader->free_code = ARCFS_LZW_FIRST_FREE;
    return true;
}

static bool arcfs_reader_next(arcfs_lzw_reader *reader, uint32_t *code) {
    uint32_t value = 0U;
    uint32_t index;
    if (!reader || !code) return false;
    if (reader->max_code < reader->free_code) {
        if (!arcfs_reader_pad(reader) || reader->width >= reader->max_bits)
            return false;
        ++reader->width;
        reader->max_code = reader->width == reader->max_bits
                               ? reader->capacity
                               : ((UINT32_C(1) << reader->width) - 1U);
    }
    /* The final code may use the pad bits of its final byte, but cannot begin
     * after the physical stream. */
    if (reader->bit_position >= reader->bit_size ||
        reader->width > SIZE_MAX - reader->bit_position)
        return false;
    for (index = 0U; index < reader->width; ++index) {
        size_t bit = reader->bit_position + (size_t)index;
        if (bit < reader->bit_size)
            value |= (uint32_t)((reader->input[bit >> 3U] >>
                                 (bit & 7U)) & 1U) << index;
    }
    reader->bit_position += reader->width;
    ++reader->group_count;
    *code = value;
    return true;
}

bool xx_arcfs_rle90_decode_memory(const uint8_t *input, size_t input_size,
                                  uint8_t *output, size_t output_size,
                                  size_t *written) {
    arcfs_sink sink;
    size_t index;
    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.output = output;
    sink.output_size = output_size;
    sink.rle90 = true;
    for (index = 0U; index < input_size; ++index) {
        if (!arcfs_sink_push(&sink, input[index])) return false;
    }
    if (written) *written = sink.position;
    return !sink.escaped && sink.position == output_size;
}

bool xx_arcfs_lzw_decode_memory(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size,
                                uint8_t max_bits, bool apply_rle90,
                                size_t *written) {
    arcfs_lzw_reader reader;
    arcfs_sink sink = { 0 };
    uint16_t *prefix = NULL;
    uint8_t *suffix = NULL;
    uint8_t *stack = NULL;
    uint32_t capacity;
    int32_t previous = -1;
    uint8_t previous_first = 0U;
    bool result = false;

    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        max_bits < 9U || max_bits > 16U || input_size > SIZE_MAX / 8U)
        return false;
    if (output_size == 0U) return input_size == 0U;
    capacity = UINT32_C(1) << max_bits;
    if ((size_t)capacity > SIZE_MAX / sizeof(*prefix)) return false;
    prefix = (uint16_t *)xx_mem_alloc((size_t)capacity * sizeof(*prefix));
    suffix = (uint8_t *)xx_mem_alloc((size_t)capacity);
    stack = (uint8_t *)xx_mem_alloc((size_t)capacity);
    if (!prefix || !suffix || !stack) goto done;
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.input = input;
    reader.bit_size = input_size * 8U;
    reader.width = 9U;
    reader.max_code = 0x1ffU;
    reader.free_code = ARCFS_LZW_FIRST_FREE;
    reader.capacity = capacity;
    reader.max_bits = max_bits;
    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.output = output;
    sink.output_size = output_size;
    sink.rle90 = apply_rle90;

    while (sink.position < output_size) {
        uint32_t code;
        uint32_t current;
        uint32_t first;
        size_t stack_size = 0U;
        if (!arcfs_reader_next(&reader, &code)) goto done;
        if (code == ARCFS_LZW_CLEAR) {
            if (!arcfs_reader_reset(&reader) || !arcfs_reader_next(&reader, &code) ||
                code > 0xffU || !arcfs_sink_push(&sink, (uint8_t)code))
                goto done;
            previous = (int32_t)code;
            previous_first = (uint8_t)code;
            continue;
        }
        if (previous < 0) {
            if (code > 0xffU || !arcfs_sink_push(&sink, (uint8_t)code))
                goto done;
            previous = (int32_t)code;
            previous_first = (uint8_t)code;
            continue;
        }
        current = code;
        if (code >= reader.free_code) {
            if (stack_size >= capacity) goto done;
            stack[stack_size++] = previous_first;
            current = (uint32_t)previous;
        }
        while (current > 0xffU) {
            if (current >= capacity || stack_size >= capacity) goto done;
            stack[stack_size++] = suffix[current];
            current = prefix[current];
        }
        first = current;
        if (stack_size >= capacity) goto done;
        stack[stack_size++] = (uint8_t)first;
        while (stack_size != 0U) {
            if (!arcfs_sink_push(&sink, stack[--stack_size])) goto done;
        }
        if (reader.free_code < capacity) {
            prefix[reader.free_code] = (uint16_t)previous;
            suffix[reader.free_code] = (uint8_t)first;
            ++reader.free_code;
        }
        previous = (int32_t)code;
        previous_first = (uint8_t)first;
    }
    result = !sink.escaped;
done:
    if (written) *written = sink.position;
    if (prefix) xx_mem_free(prefix);
    if (suffix) xx_mem_free(suffix);
    if (stack) xx_mem_free(stack);
    return result && sink.position == output_size;
}
