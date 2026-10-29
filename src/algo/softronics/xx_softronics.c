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

/* A bounded pass charges code reads, prefix links, emitted/count bytes and
 * tail bits. Legacy wrappers retain their unbounded, uncancelled contract. */
typedef struct xx_softronics_work_s {
    uint64_t remaining;
    unsigned until_poll;
    bool bounded;
    xx_softronics_poll_fn poll;
    void *context;
} xx_softronics_work;

static bool xx_softronics_charge(xx_softronics_work *work)
{
    if (work->bounded) {
        if (!work->remaining) return false;
        --work->remaining;
    }
    if (work->poll && --work->until_poll == 0U) {
        work->until_poll = 256U;
        if (!work->poll(work->context)) return false;
    }
    return true;
}

typedef struct xx_softronics_bit_reader_s {
    const uint8_t *data;
    size_t bit_size;
    size_t bit_position;
} xx_softronics_bit_reader;

static bool xx_softronics_read_code(xx_softronics_bit_reader *reader, unsigned width, uint16_t *code, xx_softronics_work *work)
{
    uint16_t value = 0U;
    unsigned index;
    if (!xx_softronics_charge(work) || !reader || !code || width == 0U || width > XX_SOFTRONICS_MAX_BITS || reader->bit_position > reader->bit_size || width > reader->bit_size - reader->bit_position) {
        return false;
    }
    for (index = 0U; index < width; ++index) {
        size_t bit = reader->bit_position + index;
        value |= (uint16_t)(((reader->data[bit >> 3U] >> (bit & 7U)) & 1U) << index);
    }
    reader->bit_position += width;
    *code = value;
    return true;
}

static bool xx_softronics_padding_is_zero(const xx_softronics_bit_reader *reader, xx_softronics_work *work)
{
    size_t bit;
    if (!reader || reader->bit_position > reader->bit_size) return false;
    for (bit = reader->bit_position; bit < reader->bit_size; ++bit) {
        if (!xx_softronics_charge(work)) return false;
        if (((reader->data[bit >> 3U] >> (bit & 7U)) & 1U) != 0U) {
            return false;
        }
    }
    return true;
}

static bool xx_softronics_expand_code(uint16_t code, uint16_t next_code, const uint16_t *prefix, const uint8_t *suffix, uint8_t *stack, size_t *stack_size,
                                      uint8_t *first_byte, xx_softronics_work *work)
{
    size_t count = 0U;
    uint16_t current = code;
    if (!prefix || !suffix || !stack || !stack_size || !first_byte || code >= next_code) {
        return false;
    }
    while (current >= XX_SOFTRONICS_CLEAR) {
        if (!xx_softronics_charge(work) || current >= next_code || count >= XX_SOFTRONICS_DICTIONARY_SIZE) {
            return false;
        }
        stack[count++] = suffix[current];
        current = prefix[current];
    }
    if (!xx_softronics_charge(work) || count >= XX_SOFTRONICS_DICTIONARY_SIZE) return false;
    stack[count++] = (uint8_t)current;
    *first_byte = (uint8_t)current;
    *stack_size = count;
    return true;
}

/* Both measurement and decoding traverse the same validated phrases. Only
 * this sink decides whether a byte is stored or merely counted. */
static bool xx_softronics_emit_byte(uint8_t *output, size_t output_size, size_t *position, uint8_t byte, xx_softronics_work *work)
{
    if (*position >= output_size || !xx_softronics_charge(work)) return false;
    if (output) output[*position] = byte;
    ++*position;
    return true;
}

static bool xx_softronics_emit_reversed(uint8_t *output, size_t output_size, size_t *output_position, const uint8_t *stack, size_t stack_size,
                                        xx_softronics_work *work)
{
    size_t index;
    if (!output_position || !stack || !stack_size || *output_position > output_size || stack_size > output_size - *output_position) return false;
    for (index = stack_size; index != 0U; --index) {
        if (!xx_softronics_emit_byte(output, output_size, output_position, stack[index - 1U], work)) return false;
    }
    return true;
}

static bool xx_softronics_decode(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *consumed_size, bool exact, bool measure, size_t *measured_size, uint64_t work_limit,
                                  xx_softronics_poll_fn poll, void *context)
{
    xx_softronics_bit_reader reader;
    xx_softronics_work work = {work_limit, 256U, work_limit != 0U, poll, context};
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
    if (measured_size) *measured_size = 0U;
    if (!input || (!output && !measure) || (measure && !measured_size) || input_size == 0U || output_size == 0U || input_size > SIZE_MAX / 8U) {
        return false;
    }
    if (poll && !poll(context)) return false;
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.data = input;
    reader.bit_size = input_size * 8U;
    if (!xx_softronics_read_code(&reader, width, &code, &work) || code != XX_SOFTRONICS_CLEAR) {
        return false;
    }
    for (;;) {
        uint8_t first_byte;
        size_t stack_size;
        if (!xx_softronics_read_code(&reader, width, &code, &work)) return false;
        if (code == XX_SOFTRONICS_CLEAR) {
            next_code = XX_SOFTRONICS_FIRST_FREE;
            width = XX_SOFTRONICS_INITIAL_BITS;
            have_previous = false;
            continue;
        }
        if (code == XX_SOFTRONICS_END) {
            if (!have_previous || (!measure && output_position != output_size) || (exact && (reader.bit_position+7U)/8U!=input_size) || !xx_softronics_padding_is_zero(&reader, &work) || (poll && !poll(context))) {
                return false;
            }
            if (consumed_size) *consumed_size = input_size;
            if (measured_size) *measured_size = output_position;
            return true;
        }
        if (!have_previous) {
            if (code >= XX_SOFTRONICS_CLEAR || output_position >= output_size) {
                return false;
            }
            if (!xx_softronics_emit_byte(output, output_size, &output_position, (uint8_t)code, &work)) return false;
            previous_code = code;
            previous_first = (uint8_t)code;
            have_previous = true;
            continue;
        }
        if (code < next_code) {
            if (!xx_softronics_expand_code(code, next_code, prefix, suffix, stack, &stack_size, &first_byte, &work) ||
                !xx_softronics_emit_reversed(output, output_size, &output_position, stack, stack_size, &work)) {
                return false;
            }
        } else if (code == next_code && next_code < XX_SOFTRONICS_DICTIONARY_SIZE) {
            if (!xx_softronics_expand_code(previous_code, next_code, prefix, suffix, stack, &stack_size, &first_byte, &work) ||
                !xx_softronics_emit_reversed(output, output_size, &output_position, stack, stack_size, &work) || output_position >= output_size) {
                return false;
            }
            if (!xx_softronics_emit_byte(output, output_size, &output_position, previous_first, &work)) return false;
            first_byte = previous_first;
        } else {
            return false;
        }
        if (next_code < XX_SOFTRONICS_DICTIONARY_SIZE) {
            prefix[next_code] = previous_code;
            suffix[next_code] = first_byte;
            ++next_code;
            if (width < XX_SOFTRONICS_MAX_BITS && next_code == (UINT16_C(1) << width)) {
                ++width;
            }
        }
        previous_code = code;
        previous_first = first_byte;
    }
}

bool xx_softronics_lzw_decompress_memory(const uint8_t *input,size_t input_size,uint8_t *output,size_t output_size,size_t *consumed_size)
{
    return xx_softronics_decode(input,input_size,output,output_size,consumed_size,false,false,NULL,0U,NULL,NULL);
}
bool xx_softronics_lzw_decompress_memory_exact(const uint8_t *input,size_t input_size,uint8_t *output,size_t output_size)
{
    return xx_softronics_decode(input,input_size,output,output_size,NULL,true,false,NULL,0U,NULL,NULL);
}

bool xx_softronics_lzw_measure_memory_exact_bounded(const uint8_t *input, size_t input_size, size_t output_limit,
                                                    uint64_t work_limit, xx_softronics_poll_fn poll, void *context, size_t *measured_size)
{
    if (measured_size) *measured_size = 0U;
    if (!work_limit) return false;
    return xx_softronics_decode(input,input_size,NULL,output_limit,NULL,true,true,measured_size,work_limit,poll,context);
}

bool xx_softronics_lzw_decompress_memory_exact_bounded(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size,
                                                       uint64_t work_limit, xx_softronics_poll_fn poll, void *context)
{
    if (!work_limit) return false;
    return xx_softronics_decode(input,input_size,output,output_size,NULL,true,false,NULL,work_limit,poll,context);
}
