/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native decoder for the LZV1 container's MSB-first, phased-in LZW code
 * stream.  The stream has no EOS code: physical EOF terminates it cleanly.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzv1/xx_lzv1.h"

#include "xxfclib/memory/xx_memory.h"

#include <string.h>

#define XX_LZV1_HEADER_SIZE 12U
#define XX_LZV1_ROOT_CODES 256U
#define XX_LZV1_MAX_CODES 0x4000U
#define XX_LZV1_MAX_OUTPUT ((size_t)512U * 1024U * 1024U)

typedef struct xx_lzv1_bit_reader_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t current;
    unsigned bits_left;
} xx_lzv1_bit_reader;

static bool xx_lzv1_read_bits(xx_lzv1_bit_reader *reader, unsigned count,
                              uint32_t *value) {
    uint32_t result = 0U;
    unsigned index;
    if (!reader || !value || count == 0U || count > 16U) return false;
    for (index = 0U; index < count; ++index) {
        if (reader->bits_left == 0U) {
            if (reader->position >= reader->size) return false;
            reader->current = reader->data[reader->position++];
            reader->bits_left = 8U;
        }
        result = (result << 1U) |
                 (uint32_t)((reader->current >> 7U) & 1U);
        reader->current = (uint8_t)(reader->current << 1U);
        --reader->bits_left;
    }
    *value = result;
    return true;
}

bool xx_lzv1_parse_header(const uint8_t *input, size_t input_size,
                          xx_lzv1_header *header) {
    static const uint8_t fixed_bytes[] = {
        0x5dU, 0x19U, 0x01U, 0xadU, 0x00U, 0x00U
    };
    uint16_t max_codes;
    if (!input || !header || input_size < XX_LZV1_HEADER_SIZE ||
        xx_rt_memcmp(input, "LZV1", 4U) != 0 ||
        xx_rt_memcmp(input + 4U, fixed_bytes, sizeof(fixed_bytes)) != 0) {
        return false;
    }
    max_codes = (uint16_t)(((uint16_t)input[10] << 8U) | input[11]);
    if (max_codes <= XX_LZV1_ROOT_CODES || max_codes >= XX_LZV1_MAX_CODES) {
        return false;
    }
    header->max_codes = max_codes;
    return true;
}

static bool xx_lzv1_append(uint8_t **output, size_t *output_size,
                           size_t *capacity, uint8_t value) {
    uint8_t *resized;
    size_t new_capacity;
    if (!output || !output_size || !capacity || *output_size >= XX_LZV1_MAX_OUTPUT) {
        return false;
    }
    if (*output_size == *capacity) {
        new_capacity = *capacity == 0U ? 256U : *capacity;
        while (new_capacity <= *output_size) {
            if (new_capacity >= XX_LZV1_MAX_OUTPUT / 2U) {
                new_capacity = XX_LZV1_MAX_OUTPUT;
                break;
            }
            new_capacity *= 2U;
        }
        resized = (uint8_t *)xx_mem_realloc(*output, new_capacity);
        if (!resized) return false;
        *output = resized;
        *capacity = new_capacity;
    }
    (*output)[(*output_size)++] = value;
    return true;
}

static bool xx_lzv1_append_code(const uint16_t *prefix,
                                const uint8_t *suffix, uint16_t max_codes,
                                uint16_t code, uint8_t *stack,
                                size_t *stack_size, uint8_t **output,
                                size_t *output_size, size_t *capacity) {
    size_t count = 0U;
    uint16_t current = code;
    if (!prefix || !suffix || !stack || !stack_size || !output ||
        !output_size || !capacity || code >= max_codes) {
        return false;
    }
    for (;;) {
        uint16_t parent;
        if (current >= max_codes || count >= (size_t)max_codes) return false;
        stack[count++] = suffix[current];
        parent = prefix[current];
        if (parent == UINT16_MAX) break;
        if (parent >= current) return false;
        current = parent;
    }
    while (count != 0U) {
        if (!xx_lzv1_append(output, output_size, capacity, stack[--count])) {
            return false;
        }
    }
    *stack_size = count;
    return true;
}

bool xx_lzv1_decompress_memory(const uint8_t *input, size_t input_size,
                               uint8_t **output, size_t *output_size) {
    xx_lzv1_header header;
    xx_lzv1_bit_reader reader;
    uint16_t *prefix = NULL;
    uint8_t *suffix = NULL;
    uint8_t *first = NULL;
    uint8_t *stack = NULL;
    uint8_t *result = NULL;
    size_t result_size = 0U;
    size_t capacity = 0U;
    bool finished = false;
    bool ok = false;
    if (!output || !output_size) return false;
    *output = NULL;
    *output_size = 0U;
    if (!input || input_size <= XX_LZV1_HEADER_SIZE ||
        !xx_lzv1_parse_header(input, input_size, &header)) {
        return false;
    }
    prefix = (uint16_t *)xx_mem_alloc((size_t)header.max_codes *
                                      sizeof(*prefix));
    suffix = (uint8_t *)xx_mem_alloc((size_t)header.max_codes);
    first = (uint8_t *)xx_mem_alloc((size_t)header.max_codes);
    stack = (uint8_t *)xx_mem_alloc((size_t)header.max_codes + 1U);
    if (!prefix || !suffix || !first || !stack) goto cleanup;
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.data = input + XX_LZV1_HEADER_SIZE;
    reader.size = input_size - XX_LZV1_HEADER_SIZE;
    while (!finished) {
        uint16_t next = XX_LZV1_ROOT_CODES;
        uint16_t limit = XX_LZV1_ROOT_CODES;
        uint16_t current;
        uint16_t previous;
        uint32_t first_code;
        size_t unused_stack_size = 0U;
        uint16_t index;
        for (index = 0U; index < XX_LZV1_ROOT_CODES; ++index) {
            prefix[index] = UINT16_MAX;
            suffix[index] = (uint8_t)index;
            first[index] = (uint8_t)index;
        }
        if (!xx_lzv1_read_bits(&reader, 8U, &first_code)) break;
        current = (uint16_t)first_code;
        previous = current;
        for (;;) {
            uint32_t raw;
            int32_t value;
            uint16_t source;
            unsigned width = 8U;
            uint16_t width_limit = limit;
            while (width_limit > XX_LZV1_ROOT_CODES) {
                ++width;
                width_limit = (uint16_t)(width_limit >> 1U);
            }
            if (!xx_lzv1_append_code(prefix, suffix, header.max_codes,
                                     current, stack, &unused_stack_size,
                                     &result, &result_size, &capacity)) {
                goto cleanup;
            }
            if ((uint16_t)(next + 1U) == header.max_codes) break;
            if ((uint32_t)limit * 2U <= (uint32_t)next + 1U) {
                limit = (uint16_t)(limit * 2U);
                ++width;
            }
            if (!xx_lzv1_read_bits(&reader, width, &raw)) {
                finished = true;
                break;
            }
            value = (int32_t)raw + (int32_t)limit - (int32_t)next - 1;
            if (value < 0) {
                uint32_t continuation;
                if (!xx_lzv1_read_bits(&reader, 1U, &continuation)) {
                    finished = true;
                    break;
                }
                value = ((int32_t)(raw + limit) * 2) - (int32_t)next - 1 +
                        (int32_t)continuation;
            }
            if (value < 0 || value > (int32_t)next || next >= header.max_codes) {
                goto cleanup;
            }
            source = (uint16_t)value == next ? previous : (uint16_t)value;
            prefix[next] = previous;
            suffix[next] = first[source];
            first[next] = first[previous];
            ++next;
            previous = (uint16_t)value;
            current = (uint16_t)value;
        }
    }
    if (result_size == 0U) goto cleanup;
    *output = result;
    *output_size = result_size;
    result = NULL;
    ok = true;
cleanup:
    xx_mem_free(result);
    xx_mem_free(stack);
    xx_mem_free(first);
    xx_mem_free(suffix);
    xx_mem_free(prefix);
    return ok;
}
