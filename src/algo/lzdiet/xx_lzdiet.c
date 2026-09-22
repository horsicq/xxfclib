/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * lZdIeT stores a fixed table of independently reset LZW chunks. Codes are
 * least-significant-bit first, use clear 0x100/end 0x101, and grow from nine
 * to ten bits without an early-change offset.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzdiet/xx_lzdiet.h"

#include <string.h>

#define XX_LZDIET_HEADER_SIZE 0x5fcU
#define XX_LZDIET_TABLE_OFFSET 0x20U
#define XX_LZDIET_ENTRY_SIZE 6U
#define XX_LZDIET_MAX_CHUNKS 250U
#define XX_LZDIET_CHUNK_PREAMBLE 6U
#define XX_LZDIET_MAX_OUTPUT UINT32_C(0x40000000)
#define XX_LZDIET_MAX_CODES 1024U
#define XX_LZDIET_CLEAR_CODE 0x100U
#define XX_LZDIET_END_CODE 0x101U
#define XX_LZDIET_FIRST_CODE 0x102U

typedef struct xx_lzdiet_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t accumulator;
    unsigned bit_count;
} xx_lzdiet_bits;

static uint16_t xx_lzdiet_read16le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_lzdiet_read32le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool xx_lzdiet_read_code(xx_lzdiet_bits *bits, unsigned *width,
                                unsigned *max_code, unsigned next_free,
                                uint32_t *code) {
    uint32_t mask;
    if (!bits || !width || !max_code || !code) return false;
    if (*max_code < next_free) {
        if (*width >= 10U) return false;
        ++*width;
        *max_code = *width == 10U ? XX_LZDIET_MAX_CODES :
                                    ((1U << *width) - 1U);
    }
    while (bits->bit_count < *width) {
        if (bits->position >= bits->size) return false;
        bits->accumulator |= (uint32_t)bits->data[bits->position++]
                             << bits->bit_count;
        bits->bit_count += 8U;
    }
    mask = (1U << *width) - 1U;
    *code = bits->accumulator & mask;
    bits->accumulator >>= *width;
    bits->bit_count -= *width;
    return true;
}

static bool xx_lzdiet_decode_chunk(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *output_position) {
    uint16_t prefix[XX_LZDIET_MAX_CODES];
    uint8_t suffix[XX_LZDIET_MAX_CODES];
    uint8_t stack[XX_LZDIET_MAX_CODES];
    xx_lzdiet_bits bits;
    unsigned width = 9U;
    unsigned max_code = 0x1ffU;
    unsigned next_free = XX_LZDIET_FIRST_CODE;
    uint32_t old_code = 0U;
    uint8_t first_char = 0U;
    size_t index;

    if (!input || !output || !output_position) {
        return false;
    }
    xx_rt_memset(&bits, 0, sizeof(bits));
    bits.data = input;
    bits.size = input_size;
    for (index = 0U; index < XX_LZDIET_MAX_CODES; ++index) {
        prefix[index] = 0U;
        suffix[index] = index < 256U ? (uint8_t)index : 0U;
    }

    while (*output_position < output_size) {
        uint32_t code;
        if (!xx_lzdiet_read_code(&bits, &width, &max_code, next_free,
                                  &code)) {
            return true;
        }
        if (code == XX_LZDIET_END_CODE) return true;
        if (code == XX_LZDIET_CLEAR_CODE) {
            width = 9U;
            max_code = 0x1ffU;
            next_free = XX_LZDIET_FIRST_CODE;
            if (!xx_lzdiet_read_code(&bits, &width, &max_code, next_free,
                                      &code)) {
                return false;
            }
            if (code == XX_LZDIET_CLEAR_CODE || code == XX_LZDIET_END_CODE) {
                return true;
            }
            if (code > 0xffU) return false;
            old_code = code;
            first_char = (uint8_t)code;
            output[(*output_position)++] = (uint8_t)code;
            continue;
        }
        {
            uint32_t work = code;
            size_t stack_size = 0U;
            if (code > next_free) return false;
            if (code == next_free) {
                if (stack_size >= sizeof(stack)) return false;
                stack[stack_size++] = first_char;
                work = old_code;
            }
            while (work > 0xffU) {
                if (work >= next_free || stack_size >= sizeof(stack)) {
                    return false;
                }
                stack[stack_size++] = suffix[work];
                work = prefix[work];
            }
            if (stack_size >= sizeof(stack)) return false;
            stack[stack_size++] = (uint8_t)work;
            first_char = (uint8_t)work;
            while (stack_size != 0U && *output_position < output_size) {
                output[(*output_position)++] = stack[--stack_size];
            }
            if (next_free < XX_LZDIET_MAX_CODES) {
                prefix[next_free] = (uint16_t)old_code;
                suffix[next_free] = first_char;
                ++next_free;
            }
            old_code = code;
        }
    }
    return true;
}

bool xx_lzdiet_parse_memory(const uint8_t *input, size_t input_size,
                            xx_lzdiet_info *info) {
    uint32_t raw_size;
    uint16_t chunk_count = 0U;
    size_t expected_offset = XX_LZDIET_HEADER_SIZE;
    size_t archive_size = XX_LZDIET_HEADER_SIZE;
    unsigned index;
    bool terminated = false;

    if (info) xx_rt_memset(info, 0, sizeof(*info));
    if (!input || input_size < XX_LZDIET_HEADER_SIZE + XX_LZDIET_CHUNK_PREAMBLE ||
        xx_rt_memcmp(input, "lZdIeT", 6U) != 0) {
        return false;
    }
    raw_size = xx_lzdiet_read32le(input + 6U);
    if ((int32_t)raw_size <= 0 || raw_size > XX_LZDIET_MAX_OUTPUT ||
        xx_lzdiet_read32le(input + XX_LZDIET_TABLE_OFFSET) !=
            XX_LZDIET_HEADER_SIZE ||
        xx_lzdiet_read16le(input + XX_LZDIET_TABLE_OFFSET + 4U) <
            XX_LZDIET_CHUNK_PREAMBLE) {
        return false;
    }
    for (index = 0U; index < XX_LZDIET_MAX_CHUNKS; ++index) {
        const uint8_t *entry = input + XX_LZDIET_TABLE_OFFSET +
                               index * XX_LZDIET_ENTRY_SIZE;
        uint32_t offset_raw = xx_lzdiet_read32le(entry);
        uint16_t chunk_size = xx_lzdiet_read16le(entry + 4U);
        size_t chunk_offset;
        if (offset_raw == UINT32_MAX) {
            terminated = true;
            break;
        }
        if ((int32_t)offset_raw < 0 ||
            chunk_size < XX_LZDIET_CHUNK_PREAMBLE) {
            return false;
        }
        chunk_offset = (size_t)offset_raw;
        if (chunk_offset < expected_offset || chunk_offset > input_size ||
            (size_t)chunk_size > input_size - chunk_offset) {
            return false;
        }
        expected_offset = chunk_offset + (size_t)chunk_size;
        archive_size = expected_offset;
        ++chunk_count;
    }
    if (!terminated && chunk_count != XX_LZDIET_MAX_CHUNKS) return false;
    if (chunk_count == 0U) return false;
    if (info) {
        info->uncompressed_size = raw_size;
        info->chunk_count = chunk_count;
        info->archive_size = archive_size;
    }
    return true;
}

bool xx_lzdiet_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size, xx_lzdiet_info *info) {
    xx_lzdiet_info parsed;
    size_t output_position = 0U;
    unsigned index;
    if (consumed_size) *consumed_size = 0U;
    if (info) xx_rt_memset(info, 0, sizeof(*info));
    if (!input || !output ||
        !xx_lzdiet_parse_memory(input, input_size, &parsed) ||
        output_size != (size_t)parsed.uncompressed_size) {
        return false;
    }
    for (index = 0U; index < parsed.chunk_count &&
                    output_position < output_size; ++index) {
        const uint8_t *entry = input + XX_LZDIET_TABLE_OFFSET +
                               index * XX_LZDIET_ENTRY_SIZE;
        size_t chunk_offset = (size_t)xx_lzdiet_read32le(entry);
        size_t chunk_size = xx_lzdiet_read16le(entry + 4U);
        if (chunk_size < XX_LZDIET_CHUNK_PREAMBLE ||
            !xx_lzdiet_decode_chunk(input + chunk_offset +
                                     XX_LZDIET_CHUNK_PREAMBLE,
                                     chunk_size - XX_LZDIET_CHUNK_PREAMBLE,
                                     output, output_size, &output_position)) {
            return false;
        }
    }
    if (output_position != output_size) return false;
    if (consumed_size) *consumed_size = parsed.archive_size;
    if (info) *info = parsed;
    return true;
}
