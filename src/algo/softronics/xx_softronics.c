/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Softronics version 2.00 stores a compact GIF-family LZW stream without GIF
 * sub-block framing.  Codes are read low-bit first, begin at nine bits, and
 * grow through twelve bits after the corresponding dictionary slot is added.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/softronics/xx_softronics.h"

#include <string.h>

#define XX_SOFTRONICS_CLEAR UINT16_C(256)
#define XX_SOFTRONICS_END UINT16_C(257)
#define XX_SOFTRONICS_FIRST_FREE UINT16_C(258)
#define XX_SOFTRONICS_DICTIONARY_SIZE 4096U
#define XX_SOFTRONICS_INITIAL_BITS 9U
#define XX_SOFTRONICS_MAX_BITS 12U

typedef struct xx_softronics_bit_reader_s {
    const uint8_t *data;
    size_t bit_size;
    size_t bit_position;
} xx_softronics_bit_reader;

static bool xx_softronics_read_code(xx_softronics_bit_reader *reader,
                                    unsigned width, uint16_t *code) {
    uint16_t value = 0U;
    unsigned index;
    if (!reader || !code || width == 0U || width > XX_SOFTRONICS_MAX_BITS ||
        reader->bit_position > reader->bit_size ||
        width > reader->bit_size - reader->bit_position) {
        return false;
    }
    for (index = 0U; index < width; ++index) {
        size_t bit = reader->bit_position + index;
        value |= (uint16_t)(((reader->data[bit >> 3U] >> (bit & 7U)) & 1U)
                            << index);
    }
    reader->bit_position += width;
    *code = value;
    return true;
}

static bool xx_softronics_padding_is_zero(
    const xx_softronics_bit_reader *reader) {
    size_t bit;
    if (!reader || reader->bit_position > reader->bit_size) return false;
    for (bit = reader->bit_position; bit < reader->bit_size; ++bit) {
        if (((reader->data[bit >> 3U] >> (bit & 7U)) & 1U) != 0U) {
            return false;
        }
    }
    return true;
}

static bool xx_softronics_expand_code(
    uint16_t code, uint16_t next_code, const uint16_t *prefix,
    const uint8_t *suffix, uint8_t *stack, size_t *stack_size,
    uint8_t *first_byte) {
    size_t count = 0U;
    uint16_t current = code;
    if (!prefix || !suffix || !stack || !stack_size || !first_byte ||
        code >= next_code) {
        return false;
    }
    while (current >= XX_SOFTRONICS_CLEAR) {
        if (current >= next_code || count >= XX_SOFTRONICS_DICTIONARY_SIZE) {
            return false;
        }
        stack[count++] = suffix[current];
        current = prefix[current];
    }
    if (count >= XX_SOFTRONICS_DICTIONARY_SIZE) return false;
    stack[count++] = (uint8_t)current;
    *first_byte = (uint8_t)current;
    *stack_size = count;
    return true;
}

static bool xx_softronics_emit_reversed(uint8_t *output, size_t output_size,
                                        size_t *output_position,
                                        const uint8_t *stack,
                                        size_t stack_size) {
    size_t index;
    if (!output || !output_position || !stack || stack_size == 0U ||
        stack_size > output_size - *output_position) {
        return false;
    }
    for (index = stack_size; index != 0U; --index) {
        output[(*output_position)++] = stack[index - 1U];
    }
    return true;
}

bool xx_softronics_lzw_decompress_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed_size) {
    xx_softronics_bit_reader reader;
    uint16_t prefix[XX_SOFTRONICS_DICTIONARY_SIZE];
    uint8_t suffix[XX_SOFTRONICS_DICTIONARY_SIZE];
    uint8_t stack[XX_SOFTRONICS_DICTIONARY_SIZE];
    uint16_t next_code = XX_SOFTRONICS_FIRST_FREE;
    uint16_t previous_code = 0U;
    uint8_t previous_first = 0U;
    size_t output_position = 0U;
    unsigned width = XX_SOFTRONICS_INITIAL_BITS;
    bool have_previous = false;
    uint16_t code;
    if (consumed_size) *consumed_size = 0U;
    if (!input || !output || input_size == 0U || output_size == 0U ||
        input_size > SIZE_MAX / 8U) {
        return false;
    }
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.data = input;
    reader.bit_size = input_size * 8U;
    if (!xx_softronics_read_code(&reader, width, &code) ||
        code != XX_SOFTRONICS_CLEAR) {
        return false;
    }
    for (;;) {
        uint8_t first_byte;
        size_t stack_size;
        if (!xx_softronics_read_code(&reader, width, &code)) return false;
        if (code == XX_SOFTRONICS_CLEAR) {
            next_code = XX_SOFTRONICS_FIRST_FREE;
            width = XX_SOFTRONICS_INITIAL_BITS;
            have_previous = false;
            continue;
        }
        if (code == XX_SOFTRONICS_END) {
            if (!have_previous || output_position != output_size ||
                !xx_softronics_padding_is_zero(&reader)) {
                return false;
            }
            if (consumed_size) *consumed_size = input_size;
            return true;
        }
        if (!have_previous) {
            if (code >= XX_SOFTRONICS_CLEAR || output_position >= output_size) {
                return false;
            }
            output[output_position++] = (uint8_t)code;
            previous_code = code;
            previous_first = (uint8_t)code;
            have_previous = true;
            continue;
        }
        if (code < next_code) {
            if (!xx_softronics_expand_code(code, next_code, prefix, suffix,
                                            stack, &stack_size, &first_byte) ||
                !xx_softronics_emit_reversed(output, output_size,
                                               &output_position, stack,
                                               stack_size)) {
                return false;
            }
        } else if (code == next_code && next_code < XX_SOFTRONICS_DICTIONARY_SIZE) {
            if (!xx_softronics_expand_code(previous_code, next_code, prefix,
                                            suffix, stack, &stack_size,
                                            &first_byte) ||
                !xx_softronics_emit_reversed(output, output_size,
                                               &output_position, stack,
                                               stack_size) ||
                output_position >= output_size) {
                return false;
            }
            output[output_position++] = previous_first;
            first_byte = previous_first;
        } else {
            return false;
        }
        if (next_code < XX_SOFTRONICS_DICTIONARY_SIZE) {
            prefix[next_code] = previous_code;
            suffix[next_code] = first_byte;
            ++next_code;
            if (width < XX_SOFTRONICS_MAX_BITS &&
                next_code == (UINT16_C(1) << width)) {
                ++width;
            }
        }
        previous_code = code;
        previous_first = first_byte;
    }
}
