/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Commodore 64 "Wraptor" LZSS.  Ported from XArchive/Algos/xc64wraptordecoder.cpp.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/c64wraptor/xx_c64wraptor.h"

#define C64_WINDOW_SIZE 0x1000U
#define C64_WINDOW_MASK 0x0fffU
#define C64_START_WIDTH 8
#define C64_MAX_WIDTH 0x0d
#define C64_LENGTH_BITS 5

typedef struct c64_bits {
    const uint8_t *input;
    size_t input_size;
    size_t offset;
    uint32_t accumulator;
    int count;
} c64_bits;

/* MSB-first.  Returns -1 on exhaustion. */
static int c64_bit(c64_bits *reader)
{
    int value;
    if (reader->count == 0) {
        if (reader->offset >= reader->input_size) return -1;
        reader->accumulator = reader->input[reader->offset];
        reader->offset++;
        reader->count = 8;
    }
    value = (int)((reader->accumulator >> 7) & 1U);
    reader->accumulator = (reader->accumulator << 1) & 0xffU;
    reader->count--;
    return value;
}

/* Multi-bit values accumulate most significant bit first. */
static bool c64_bits_read(c64_bits *reader, int need, int *value)
{
    int i;
    int result = 0;
    for (i = 0; i < need; ++i) {
        int bit = c64_bit(reader);
        if (bit < 0) return false;
        result = (result * 2) + bit;
    }
    *value = result;
    return true;
}

/* One member stream.  `output` may be NULL, in which case the walk only
 * counts -- the ring buffer still has to be maintained because matches read
 * from it.  This is the single core the decode and the measure share. */
static bool c64_run(const uint8_t *input, size_t input_size, uint8_t *output,
                    size_t output_capacity, size_t *produced_out,
                    size_t *consumed_out)
{
    uint8_t window[C64_WINDOW_SIZE];
    c64_bits reader;
    uint32_t write_position = 0U;
    int width = C64_START_WIDTH;
    size_t produced = 0U;
    bool complete = false;

    if (!input || !produced_out || !consumed_out) return false;

    /* The ring starts zero filled and matches may legitimately read those
     * zeros before anything has been written: format, not optimisation. */
    xx_rt_memset(window, 0, sizeof(window));

    reader.input = input;
    reader.input_size = input_size;
    reader.offset = 0U;
    reader.accumulator = 0U;
    reader.count = 0;

    for (;;) {
        int tag = c64_bit(&reader);
        if (tag < 0) break;

        if (tag == 0) {
            int literal = 0;
            if (!c64_bits_read(&reader, 8, &literal)) break;
            if (produced >= output_capacity) return false;
            if (output) output[produced] = (uint8_t)literal;
            produced++;
            window[write_position] = (uint8_t)literal;
            write_position = (write_position + 1U) & C64_WINDOW_MASK;
        } else {
            int match_offset = 0;
            int length = 0;
            int i;

            if (!c64_bits_read(&reader, width, &match_offset)) break;

            if (match_offset == 0) {
                int escape = c64_bit(&reader);
                if (escape < 0) break;
                if (escape == 0) {
                    complete = true;
                    break;
                }
                width++;
                /* Deliberate, and matches the reference: the bound is tested
                 * AFTER the increment, so a stream may widen to 12 bits but
                 * reaching 13 aborts (and an abort here is a failure, because
                 * only the end escape completes a stream). */
                if (width >= C64_MAX_WIDTH) break;
                continue;
            }

            if (!c64_bits_read(&reader, C64_LENGTH_BITS, &length)) break;
            match_offset--;

            for (i = 0; i < length; ++i) {
                uint8_t byte = window[(uint32_t)match_offset & C64_WINDOW_MASK];
                if (produced >= output_capacity) return false;
                if (output) output[produced] = byte;
                produced++;
                window[write_position] = byte;
                write_position = (write_position + 1U) & C64_WINDOW_MASK;
                match_offset++;
            }
        }
    }

    /* Running out of bits is not an end of stream: only the explicit escape
     * is.  A truncated stream therefore fails rather than reporting a short
     * but "successful" decode. */
    if (!complete) return false;

    *produced_out = produced;
    *consumed_out = reader.offset;

    return true;
}

XXFC_API bool xx_c64wraptor_decode_memory(const uint8_t *input,
                                          size_t input_size, uint8_t *output,
                                          size_t output_size, size_t *written)
{
    size_t produced = 0U;
    size_t consumed = 0U;

    if (written) *written = 0U;
    if (!input || (!output && (output_size != 0U))) return false;

    if (!c64_run(input, input_size, output, output_size, &produced, &consumed)) {
        return false;
    }
    /* The container declares the member size; the stream must produce exactly
     * it, as the reference requires. */
    if (produced != output_size) return false;

    if (written) *written = produced;

    return true;
}

XXFC_API bool xx_c64wraptor_scan_memory(const uint8_t *input,
                                        size_t input_size, size_t max_output,
                                        size_t *consumed, size_t *produced)
{
    size_t produced_local = 0U;
    size_t consumed_local = 0U;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!input || (max_output == 0U)) return false;

    if (!c64_run(input, input_size, NULL, max_output, &produced_local,
                 &consumed_local)) {
        return false;
    }

    if (consumed) *consumed = consumed_local;
    if (produced) *produced = produced_local;

    return true;
}
