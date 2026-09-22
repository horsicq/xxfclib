/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/algo/aldus/xx_aldus_lzw.h"

#define ALDUS_LZW_CLEAR 256U
#define ALDUS_LZW_EOD 257U
#define ALDUS_LZW_FIRST_FREE 258U
#define ALDUS_LZW_TABLE_SIZE 4096U

static bool aldus_lzw_get(const uint8_t *input, size_t input_size,
                          size_t *bit_offset, unsigned width,
                          unsigned *value) {
    size_t index;
    size_t bit_size;
    unsigned result = 0U;
    if (!input || !bit_offset || !value || input_size > SIZE_MAX / 8U ||
        width == 0U || width > 12U)
        return false;
    bit_size = input_size * 8U;
    if (*bit_offset > bit_size || width > bit_size - *bit_offset) return false;
    for (index = 0U; index < width; ++index) {
        size_t bit = *bit_offset + index;
        result = (result << 1U) |
                 ((unsigned)(input[bit >> 3U] >> (7U - (bit & 7U))) & 1U);
    }
    *bit_offset += width;
    *value = result;
    return true;
}

bool xx_aldus_lzw_decode_block(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size,
                                size_t *written) {
    uint16_t prefix[ALDUS_LZW_TABLE_SIZE];
    uint8_t suffix[ALDUS_LZW_TABLE_SIZE];
    uint8_t stack[ALDUS_LZW_TABLE_SIZE];
    size_t bit_offset = 0U, output_offset = 0U;
    unsigned width = 9U, next = ALDUS_LZW_FIRST_FREE;
    int previous = -1;
    uint8_t previous_first = 0U;
    bool saw_eod = false;
    size_t bit_size;

    if (written) *written = 0U;
    if (!input || !output || input_size == 0U || output_size == 0U ||
        input_size > SIZE_MAX / 8U)
        return false;
    bit_size = input_size * 8U;

    while (bit_offset <= bit_size && width <= bit_size - bit_offset) {
        unsigned code;
        unsigned current;
        size_t stack_size = 0U;
        uint8_t first;

        if (!aldus_lzw_get(input, input_size, &bit_offset, width, &code))
            return false;
        if (code == ALDUS_LZW_CLEAR) {
            width = 9U;
            next = ALDUS_LZW_FIRST_FREE;
            previous = -1;
            continue;
        }
        if (code == ALDUS_LZW_EOD) {
            saw_eod = true;
            break;
        }
        if (code > next || code >= ALDUS_LZW_TABLE_SIZE)
            return false;

        current = code;
        if (current == next) {
            if (previous < 0) return false;
            stack[stack_size++] = previous_first;
            current = (unsigned)previous;
        }
        while (current >= ALDUS_LZW_CLEAR) {
            if (current >= ALDUS_LZW_TABLE_SIZE ||
                stack_size >= ALDUS_LZW_TABLE_SIZE)
                return false;
            stack[stack_size++] = suffix[current];
            current = prefix[current];
        }
        if (stack_size >= ALDUS_LZW_TABLE_SIZE || current > 255U)
            return false;
        first = (uint8_t)current;
        stack[stack_size++] = first;
        if (stack_size > output_size - output_offset) return false;
        while (stack_size != 0U)
            output[output_offset++] = stack[--stack_size];

        if (previous >= 0 && next < ALDUS_LZW_TABLE_SIZE) {
            prefix[next] = (uint16_t)previous;
            suffix[next] = first;
            ++next;
        }
        previous = (int)code;
        previous_first = first;
        if (width < 12U && next + 1U >= (1U << width)) ++width;
    }

    if (written) *written = output_offset;
    return saw_eod && output_offset == output_size;
}
