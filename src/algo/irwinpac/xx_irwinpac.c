/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Port of XArchive/Algos/xirwinpacdecoder.cpp.  The reference is a streaming
 * device decoder; this is the same state machine over a memory buffer, with
 * one core routine used by both the decode and the measure entry points so
 * the two can never disagree.
 */
#include "xxfclib/algo/irwinpac/xx_irwinpac.h"
#include "xxfclib/memory/xx_memory.h"

/* Both the chunk size and the unpacked size are u16 fields, so a block can
 * never be larger than this; the whole block is kept in memory because matches
 * address it directly instead of a ring window. */
#define IRWINPAC_CHUNK_HEADER_SIZE 10U
#define IRWINPAC_MAX_BLOCK 65536U
#define IRWINPAC_SHORT_DIST_BITS 7U
#define IRWINPAC_LONG_DIST_BITS 11U

typedef struct irwinpac_reader {
    const uint8_t *input;
    size_t input_size;
    size_t offset;
    /* Bytes of the current chunk payload that the bit reader may still fetch. */
    size_t payload_left;
    uint32_t bit_buffer;
    unsigned bit_count;
} irwinpac_reader;

static bool irwinpac_read_byte(irwinpac_reader *reader, uint8_t *value)
{
    if (reader->offset >= reader->input_size) return false;
    *value = reader->input[reader->offset++];
    return true;
}

/* MSB-first inside each byte, and never beyond the current chunk payload: a
 * truncated block must fail instead of pulling the next block's header in as
 * if it were compressed data. */
static bool irwinpac_get_bits(irwinpac_reader *reader, unsigned bits,
                              uint32_t *value)
{
    uint32_t result = 0U;
    unsigned take;
    uint32_t part;
    uint8_t byte;

    while (bits > 0U) {
        if (reader->bit_count == 0U) {
            if (reader->payload_left == 0U) return false;
            byte = 0U;
            if (!irwinpac_read_byte(reader, &byte)) return false;
            reader->payload_left--;
            reader->bit_buffer = (uint32_t)byte;
            reader->bit_count = 8U;
        }

        take = (bits < reader->bit_count) ? bits : reader->bit_count;
        part = (reader->bit_buffer >> (reader->bit_count - take)) &
               ((1U << take) - 1U);
        result = (result << take) | part;
        reader->bit_count -= take;
        bits -= take;
    }

    *value = result;
    return true;
}

/* 2 bits (2..4), then 2 bits (5..7), then a chain of nibbles where 15 escapes
 * into the next 15-wide bucket.  The bucket base is capped at the largest
 * block a u16 unpacked size can describe so a corrupt run of 0xF nibbles
 * terminates.  Deliberate: the cap is a termination guard, not a length
 * limit - the per-block overrun test below is what bounds a real match. */
static bool irwinpac_get_length(irwinpac_reader *reader, uint32_t *length)
{
    uint32_t value = 0U;
    uint32_t base;

    if (!irwinpac_get_bits(reader, 2U, &value)) return false;
    if (value < 3U) {
        *length = 2U + value;
        return true;
    }

    if (!irwinpac_get_bits(reader, 2U, &value)) return false;
    if (value < 3U) {
        *length = 5U + value;
        return true;
    }

    base = 8U;

    for (;;) {
        if (!irwinpac_get_bits(reader, 4U, &value)) return false;
        if (value < 15U) {
            *length = base + value;
            return true;
        }
        base += 15U;
        if (base > IRWINPAC_MAX_BLOCK) return false;
    }
}

/* Decodes one compressed block into block.  The block is complete only when it
 * produced exactly the declared number of bytes: a match that would overrun
 * the declared size is a decode failure, not something to clamp. */
static bool irwinpac_decode_block(irwinpac_reader *reader, uint8_t *block,
                                  size_t block_size)
{
    size_t position = 0U;
    size_t source;
    size_t index;
    uint32_t flag;
    uint32_t is_short;
    uint32_t literal;
    uint32_t distance;
    uint32_t length;

    while (position < block_size) {
        flag = 0U;
        if (!irwinpac_get_bits(reader, 1U, &flag)) return false;

        if (flag == 0U) {
            literal = 0U;
            if (!irwinpac_get_bits(reader, 8U, &literal)) return false;
            block[position++] = (uint8_t)literal;
            continue;
        }

        is_short = 0U;
        if (!irwinpac_get_bits(reader, 1U, &is_short)) return false;

        distance = 0U;
        if (!irwinpac_get_bits(reader,
                               (is_short != 0U) ? IRWINPAC_SHORT_DIST_BITS
                                                : IRWINPAC_LONG_DIST_BITS,
                               &distance)) {
            return false;
        }

        length = 0U;
        if (!irwinpac_get_length(reader, &length)) return false;

        if ((distance == 0U) || ((size_t)distance > position)) return false;
        if (length == 0U) return false;
        if ((size_t)length > (block_size - position)) return false;

        source = position - (size_t)distance;

        /* Byte by byte on purpose: overlapping matches (distance < length)
         * are legal and must replicate. */
        for (index = 0U; index < (size_t)length; ++index) {
            block[position++] = block[source++];
        }
    }

    return true;
}

/* One core routine.  output may be NULL, in which case every block is decoded
 * into the scratch block and discarded - matches never cross a block boundary,
 * so discarding costs the measure pass nothing in fidelity.
 *
 * DELIBERATE DIVERGENCE from the reference: XIrwinPacDecoder stops cleanly as
 * soon as nProcessedLimit bytes have been produced, because it serves a
 * windowed streaming read and XBinary::_writeDevice clips the overhanging
 * block rather than failing.  A memory decoder has no window, so stopping at
 * the caller's capacity would mean returning true after producing fewer bytes
 * than the chain encodes - the one failure a caller cannot detect.  This walks
 * the chain to its end and treats an overrun of `cap` as a failure. */
static bool irwinpac_run(const uint8_t *input, size_t input_size,
                         uint8_t *output, size_t cap,
                         size_t *consumed, size_t *produced)
{
    irwinpac_reader reader;
    uint8_t *block;
    uint8_t header[IRWINPAC_CHUNK_HEADER_SIZE];
    size_t header_read;
    size_t total = 0U;
    size_t payload_size;
    size_t block_size;
    size_t stored;
    size_t index;
    uint32_t flag;
    uint32_t chunk_size;
    uint32_t unpacked_size;
    uint8_t byte;
    bool ok = true;
    bool done = false;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;

    if (!input && (input_size > 0U)) return false;

    block = (uint8_t *)xx_mem_alloc(IRWINPAC_MAX_BLOCK);
    if (!block) return false;

    reader.input = input;
    reader.input_size = input_size;
    reader.offset = 0U;
    reader.payload_left = 0U;
    reader.bit_buffer = 0U;
    reader.bit_count = 0U;

    while (!done) {
        header_read = 0U;
        reader.payload_left = 0U;
        reader.bit_count = 0U;

        /* The header is read as raw bytes, outside the bit reader's payload
         * budget; the loop below is what bounds it. */
        while ((header_read < IRWINPAC_CHUNK_HEADER_SIZE) &&
               irwinpac_read_byte(&reader, &header[header_read])) {
            header_read++;
        }

        if (header_read == 0U) {
            /* A clean end of the bounded input on a chunk boundary is how the
             * member finishes; there is no explicit terminator record. */
            done = true;
            break;
        }

        if (header_read != IRWINPAC_CHUNK_HEADER_SIZE) {
            ok = false;
            break;
        }

        flag = (uint32_t)header[0] | ((uint32_t)header[1] << 8);
        chunk_size = (uint32_t)header[2] | ((uint32_t)header[3] << 8);
        unpacked_size = (uint32_t)header[4] | ((uint32_t)header[5] << 8);
        /* header[6..9] is encoder scratch memory; it differs between files
         * that otherwise decode identically, so it carries no information. */

        if ((flag > 1U) || (chunk_size < IRWINPAC_CHUNK_HEADER_SIZE)) {
            ok = false;
            break;
        }

        payload_size = (size_t)chunk_size - IRWINPAC_CHUNK_HEADER_SIZE;
        block_size = (size_t)unpacked_size;

        if (flag == 0U) {
            if (payload_size != block_size) {
                ok = false;
                break;
            }
        } else if (block_size == 0U) {
            ok = false;
            break;
        }

        reader.payload_left = payload_size;
        reader.bit_count = 0U;

        if (block_size > 0U) {
            if (flag == 0U) {
                stored = 0U;
                while (stored < block_size) {
                    byte = 0U;
                    if ((reader.payload_left == 0U) ||
                        !irwinpac_read_byte(&reader, &byte)) {
                        break;
                    }
                    reader.payload_left--;
                    block[stored++] = byte;
                }
                if (stored != block_size) {
                    ok = false;
                    break;
                }
            } else if (!irwinpac_decode_block(&reader, block, block_size)) {
                ok = false;
                break;
            }

            /* Running out of capacity is a failure, never a truncation. */
            if (block_size > (cap - total)) {
                ok = false;
                break;
            }

            if (output) {
                for (index = 0U; index < block_size; ++index) {
                    output[total + index] = block[index];
                }
            }

            total += block_size;
        }

        /* Every block ends with a few spare bytes the encoder never uses; step
         * over whatever is left so the next chunk header is read
         * byte-aligned. */
        while (reader.payload_left > 0U) {
            byte = 0U;
            if (!irwinpac_read_byte(&reader, &byte)) {
                ok = false;
                break;
            }
            reader.payload_left--;
        }

        if (!ok) break;
    }

    xx_mem_free(block);

    if (!ok || !done) return false;

    if (consumed) *consumed = reader.offset;
    if (produced) *produced = total;

    return true;
}

bool xx_irwinpac_decode_memory(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               size_t *written)
{
    size_t produced = 0U;
    bool ok;

    if (written) *written = 0U;
    if (!output && (output_size > 0U)) return false;

    ok = irwinpac_run(input, input_size, output, output_size, NULL,
                      &produced);
    if (!ok) return false;

    if (written) *written = produced;

    return true;
}

bool xx_irwinpac_scan_memory(const uint8_t *input, size_t input_size,
                             size_t max_output, size_t *consumed,
                             size_t *produced)
{
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;

    return irwinpac_run(input, input_size, NULL, max_output, consumed,
                        produced);
}
