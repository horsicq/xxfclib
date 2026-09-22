/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QNX Neutrino boot-image payload: a chain of UCL NRV2B streams.
 * Ported from XArchive/Algos/xqnxbasedecoder.cpp.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/qnxbase/xx_qnxbase.h"

/* Largest match offset the reference accepts before declaring the stream
 * corrupt.  It is also exactly the offset the end-of-stream marker produces,
 * so the limit must not be tightened. */
#define QNXB_MAX_M_OFF 0x1000002U
/* A block length word is a u16, so no single stream can be longer. */
#define QNXB_MAX_BLOCK 0xffffU

/* Bit source (MSB first, sentinel-bit trick): the state is a 32-bit word
 * doubled on every bit; when its low byte reaches 0 the next input byte is
 * loaded into the low byte and the word is doubled and incremented, which
 * puts a stop bit under the 8 payload bits.  The bit handed back is always
 * bit 8. */
typedef struct qnx_bits {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t state;
    bool error;
} qnx_bits;

static uint32_t qnx_bit(qnx_bits *reader) {
    reader->state = reader->state * 2U;
    if ((reader->state & 0xffU) == 0U) {
        if (reader->pos >= reader->size) {
            reader->error = true;
            return 0U;
        }
        reader->state = (reader->state & 0xffffff00U) |
                        (uint32_t)reader->data[reader->pos];
        reader->pos++;
        reader->state = reader->state * 2U + 1U;
    }
    return (reader->state >> 8U) & 1U;
}

static uint32_t qnx_byte(qnx_bits *reader) {
    uint32_t result;
    if (reader->pos >= reader->size) {
        reader->error = true;
        return 0U;
    }
    result = (uint32_t)reader->data[reader->pos];
    reader->pos++;
    return result;
}

/* One complete NRV2B stream.  Writes into the caller's output buffer, which
 * also carries the match history; returns false on a malformed stream, true
 * when the end marker was reached.  *consumed receives the number of input
 * bytes the stream used. */
static bool qnx_decode_block(const uint8_t *data, size_t size, uint8_t *output,
                             size_t out_capacity, size_t *out_position,
                             size_t *consumed) {
    qnx_bits reader;
    uint32_t last_offset = 1U;
    size_t out_pos = *out_position;

    reader.data = data;
    reader.size = size;
    reader.pos = 0U;
    reader.state = 0U;
    reader.error = false;

    for (;;) {
        uint32_t offset = 1U;
        uint32_t high;
        uint32_t low;
        uint64_t length;
        size_t source;
        size_t i;

        for (;;) {
            uint32_t bit = qnx_bit(&reader);
            if (reader.error) return false;
            if (!bit) break;
            {
                uint32_t literal = qnx_byte(&reader);
                if (reader.error) return false;
                if (out_pos >= out_capacity) return false;
                output[out_pos++] = (uint8_t)literal;
            }
        }

        for (;;) {
            uint32_t bit;
            offset = offset * 2U + qnx_bit(&reader);
            if (reader.error) return false;
            if (offset > QNXB_MAX_M_OFF) return false;
            bit = qnx_bit(&reader);
            if (reader.error) return false;
            if (bit) break;
        }

        if (offset == 2U) {
            offset = last_offset;
        } else {
            /* The loop above leaves offset >= 3 whenever it is not 2, so the
             * subtraction below cannot wrap. */
            uint32_t extra = qnx_byte(&reader);
            if (reader.error) return false;
            offset = (offset - 3U) * 0x100U + extra;
            if (offset == 0xffffffffU) {
                *out_position = out_pos;
                *consumed = reader.pos;
                return true;
            }
            offset++;
            last_offset = offset;
        }

        high = qnx_bit(&reader);
        if (reader.error) return false;
        low = qnx_bit(&reader);
        if (reader.error) return false;
        length = (uint64_t)high * 2U + low + 1U;
        if (length == 1U) {
            for (;;) {
                uint32_t bit;
                length = length * 2U + qnx_bit(&reader);
                if (reader.error) return false;
                if (length > 0x7fffffffU) return false;
                bit = qnx_bit(&reader);
                if (reader.error) return false;
                if (bit) break;
            }
            length += 3U;
        }
        /* Deliberate NRV2B quirk: a far match encodes one byte less. */
        if (offset > 0xd00U) length++;

        if ((uint64_t)offset > (uint64_t)out_pos) return false;
        if (length > (uint64_t)(out_capacity - out_pos)) return false;

        source = out_pos - (size_t)offset;
        for (i = 0U; i < (size_t)length; i++) {
            output[out_pos + i] = output[source + i];
        }
        out_pos += (size_t)length;
    }
}

bool xx_qnxbase_decode_memory(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size,
                              size_t *written) {
    size_t position = 0U;
    size_t out_pos = 0U;

    if (written) *written = 0U;
    if (!input || !output || output_size == 0U) return false;

    for (;;) {
        size_t block_size;
        size_t consumed = 0U;

        if (input_size - position < 2U) return false;
        block_size = ((size_t)input[position] << 8U) |
                     (size_t)input[position + 1U];
        position += 2U;
        if (block_size == 0U) break;
        if (block_size > QNXB_MAX_BLOCK) return false;
        if (block_size > input_size - position) return false;

        if (!qnx_decode_block(input + position, block_size, output, output_size,
                              &out_pos, &consumed)) {
            return false;
        }
        /* Each block's declared length must match what the end marker
         * consumed; a mismatch means the chain is not really a QNX block
         * chain. */
        if (consumed != block_size) return false;
        position += block_size;
    }

    if (out_pos == 0U) return false;

    if (written) *written = out_pos;
    return true;
}
