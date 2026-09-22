/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * NPack ("MSTSM") payload codec, ported from XArchive/Algos/xnpackdecoder.cpp.
 *
 * The reference splits the work in two: probeStream() walks the bit grammar
 * counting bytes (nothing in the grammar depends on the plaintext) and
 * decompress() walks it again through a 2048-byte ring writing real bytes.
 * This port keeps ONE core routine that always maintains the ring and writes
 * to the caller's buffer only when it has one; the measure and the decode
 * therefore cannot disagree.  That is output-equivalent to the reference:
 * probeStream() and decompress() implement the same grammar with the same
 * error rules, and both share the reference's npackReadMatchLength().
 *
 * Deliberately kept from the reference, do not "fix":
 *
 *  - A match whose distance exceeds the number of bytes produced so far is a
 *    HARD ERROR, not a read of the zero pre-fill.  A genuine NPack stream
 *    never reads the pre-fill, and this is the reference's main structural
 *    discriminator (see the comment in XNPackDecoder::probeStream).
 *  - The 11-bit distance form treats code 0 as an error; only the 7-bit form
 *    uses code 0, as the stop code.
 *  - Running out of input mid-token is an error: an LZS block is defined to
 *    end with its stop code, so a token needing bits past EOF is broken.
 *  - A match length may legitimately exceed the 2048-byte window (the corpus
 *    maximum is 60466); the copy runs byte-by-byte through the ring, so an
 *    overlapping/self-referential match reproduces the reference exactly.
 *  - The 64 MiB length cap only stops a corrupt run of 0xf nibbles from
 *    spinning forever; it is checked after each group, as in the reference.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/npack/xx_npack.h"

#define NPACK_WINDOW_SIZE 2048U
#define NPACK_WINDOW_MASK 2047U
/* Reference: NPACK_MAX_MATCH_LENGTH = 0x4000000 (64 MiB). */
#define NPACK_MAX_MATCH_LENGTH 0x4000000U
/* Reference: NPACK_LEN_LONG_CODE = 6. */
#define NPACK_LEN_LONG_CODE 6U

typedef struct npack_bits {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t buffer;
    unsigned live;
    bool eof; /* sticky, exactly like the reference reader */
} npack_bits;

/* MSB-first, one byte of refill at a time.  Returns 0 and latches eof when the
 * payload is exhausted -- the caller must test eof after every read, which is
 * the reference's own protocol. */
static uint32_t npack_get_bits(npack_bits *reader, unsigned count)
{
    uint32_t result = 0U;
    unsigned i;

    for (i = 0U; i < count; ++i) {
        if (reader->live == 0U) {
            if (reader->pos >= reader->size) {
                reader->eof = true;
                return 0U;
            }
            reader->buffer = (uint32_t)reader->data[reader->pos];
            reader->pos++;
            reader->live = 8U;
        }
        reader->live--;
        result = (result << 1) | ((reader->buffer >> reader->live) & 1U);
    }

    return result;
}

/* Reference npackReadMatchLength().  Returns false only when the length runs
 * away past the cap; an eof during the read yields extra == 0, which ends the
 * group loop, and the caller's eof test then rejects the stream. */
static bool npack_read_match_length(npack_bits *reader, uint64_t *length)
{
    uint32_t code;
    uint64_t total;

    code = npack_get_bits(reader, 2);
    if (code == 3U) {
        code = 3U + npack_get_bits(reader, 2);
    }

    if (code < NPACK_LEN_LONG_CODE) {
        *length = (uint64_t)code + 2U;
        return true;
    }

    total = 8U;
    for (;;) {
        uint32_t extra = npack_get_bits(reader, 4);
        total += (uint64_t)extra;
        if (total > (uint64_t)NPACK_MAX_MATCH_LENGTH) return false;
        if (extra < 15U) break;
    }

    *length = total;
    return true;
}

typedef struct npack_state {
    uint8_t window[NPACK_WINDOW_SIZE];
    uint32_t window_pos;
    uint8_t *output;   /* NULL when measuring */
    size_t output_cap; /* the hard ceiling in both modes */
    size_t produced;
} npack_state;

static bool npack_emit(npack_state *state, uint8_t byte)
{
    if (state->produced >= state->output_cap) return false;
    if (state->output) state->output[state->produced] = byte;
    state->window[state->window_pos] = byte;
    state->window_pos = (state->window_pos + 1U) & NPACK_WINDOW_MASK;
    state->produced++;
    return true;
}

/* The one core routine.  `output` may be NULL to measure only. */
static bool npack_run(const uint8_t *input, size_t input_size, uint8_t *output,
                      size_t output_cap, size_t *produced, size_t *consumed)
{
    npack_bits reader;
    npack_state state;
    bool stop_code = false;

    if (produced) *produced = 0U;
    if (consumed) *consumed = 0U;
    if (!input || (input_size == 0U)) return false;

    reader.data = input;
    reader.size = input_size;
    reader.pos = 0U;
    reader.buffer = 0U;
    reader.live = 0U;
    reader.eof = false;

    xx_rt_memset(state.window, 0, sizeof(state.window));
    state.window_pos = 0U;
    state.output = output;
    state.output_cap = output_cap;
    state.produced = 0U;

    for (;;) {
        uint32_t is_match;
        uint32_t distance;
        uint64_t length;
        uint64_t i;

        is_match = npack_get_bits(&reader, 1);
        if (reader.eof) return false;

        if (is_match == 0U) {
            uint32_t literal = npack_get_bits(&reader, 8);
            if (reader.eof) return false;
            if (!npack_emit(&state, (uint8_t)literal)) return false;
            continue;
        }

        if (npack_get_bits(&reader, 1) == 0U) {
            if (reader.eof) return false;
            distance = npack_get_bits(&reader, 11);
            if (reader.eof) return false;
            /* The 11-bit form never encodes distance 0. */
            if (distance == 0U) return false;
        } else {
            if (reader.eof) return false;
            distance = npack_get_bits(&reader, 7);
            if (reader.eof) return false;
            if (distance == 0U) {
                stop_code = true;
                break;
            }
        }

        /* Deliberate: a genuine stream never reads the window pre-fill. */
        if ((uint64_t)distance > (uint64_t)state.produced) return false;

        if (!npack_read_match_length(&reader, &length)) return false;
        if (reader.eof) return false;

        for (i = 0U; i < length; ++i) {
            uint32_t source = (state.window_pos - distance) & NPACK_WINDOW_MASK;
            if (!npack_emit(&state, state.window[source])) return false;
        }
    }

    if (!stop_code) return false;

    if (produced) *produced = state.produced;
    /* Byte-aligned end of the block: the reader refills one byte at a time, so
     * reader.pos is already the rounded-up consumed count. */
    if (consumed) *consumed = reader.pos;

    return true;
}

bool xx_npack_decode_memory(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size,
                            size_t *written)
{
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!output) return false;
    if (!npack_run(input, input_size, output, output_size, &produced, NULL)) {
        return false;
    }
    if (produced != output_size) return false;
    if (written) *written = produced;
    return true;
}

bool xx_npack_scan_memory(const uint8_t *input, size_t input_size,
                          size_t max_output, size_t *consumed,
                          size_t *produced)
{
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (max_output == 0U) return false;
    return npack_run(input, input_size, NULL, max_output, produced, consumed);
}
