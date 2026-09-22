/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Stylus "DP"/SDC dictionary codec, ported from XArchive/Algos/
 * xstylusdecoder.cpp.
 *
 * Every input byte is XORed with 0xB5 before it is interpreted.  What is left
 * is textbook LZSS: an LSB-first flag byte per eight items, a literal for a
 * set bit, and a two-byte (position, length) pair for a clear one.  Two things
 * keep it from being the SZDD LZSS:
 *
 *   * the 4 KiB ring starts ZERO-filled with the write cursor at 0, and
 *   * the stored match position is biased by +18 (= F), not SZDD's +16.
 *
 * The reference keeps the ring itself as the output buffer and hands it over
 * whenever the cursor wraps.  This port writes each produced byte to the
 * caller's buffer AND to the ring; that is output-equivalent, because the
 * reference's flush emits the ring in cursor order and the ring retains
 * exactly the last 4096 produced bytes either way.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/stylus/xx_stylus.h"

#define STYLUS_WINDOW_SIZE 4096U
#define STYLUS_WINDOW_MASK (STYLUS_WINDOW_SIZE - 1U)
/* The encoder's write cursor started at N - F, so a stored position has to be
 * shifted by F to be read against a cursor that starts at 0. */
#define STYLUS_MATCH_BIAS 18U
#define STYLUS_MATCH_MIN_LENGTH 3U
#define STYLUS_XOR_KEY 0xb5U

typedef struct stylus_state {
    const uint8_t *input;
    size_t input_size;
    size_t offset;
    uint8_t *output;      /* NULL when only measuring */
    size_t limit;         /* output capacity / measuring ceiling */
    size_t produced;
    uint8_t window[STYLUS_WINDOW_SIZE];
    uint32_t cursor;
} stylus_state;

/* -1 means "input exhausted", which is the stream's only terminator. */
static int stylus_get(stylus_state *state) {
    uint8_t byte;
    if (state->offset >= state->input_size) return -1;
    byte = state->input[state->offset++];
    return (int)(uint8_t)(byte ^ STYLUS_XOR_KEY);
}

static bool stylus_emit(stylus_state *state, uint8_t byte) {
    if (state->produced >= state->limit) return false;
    if (state->output) state->output[state->produced] = byte;
    state->window[state->cursor] = byte;
    state->cursor = (state->cursor + 1U) & STYLUS_WINDOW_MASK;
    ++state->produced;
    return true;
}

static bool stylus_run(const uint8_t *input, size_t input_size,
                       uint8_t *output, size_t limit, size_t *produced,
                       size_t *consumed) {
    stylus_state state;
    uint32_t flags = 0U;

    if (!input && input_size) return false;

    state.input = input;
    state.input_size = input_size;
    state.offset = 0U;
    state.output = output;
    state.limit = limit;
    state.produced = 0U;
    state.cursor = 0U;
    xx_rt_memset(state.window, 0, sizeof(state.window));

    for (;;) {
        int byte = stylus_get(&state);
        if (byte < 0) break;

        flags >>= 1;
        if ((flags & 0x100U) == 0U) {
            /* The byte just read was the flag byte for the next eight items;
             * 0xFF00 marks the eight valid bit positions. */
            flags = (uint32_t)byte | 0xff00U;
            byte = stylus_get(&state);
            /* DELIBERATE, matches the reference: the format has no end marker,
             * so input exhaustion terminates the stream cleanly even here,
             * between a flag byte and its first item.  Do not "fix" this into
             * a mid-token error; the container's CRC-32 is what confirms the
             * result. */
            if (byte < 0) break;
        }

        if (flags & 1U) {
            if (!stylus_emit(&state, (uint8_t)byte)) return false;
        } else {
            int second = stylus_get(&state);
            uint32_t source;
            uint32_t length;
            uint32_t i;
            if (second < 0) break; /* same deliberate tolerance as above */

            source = ((((uint32_t)second >> 4) << 8) | (uint32_t)byte) +
                     STYLUS_MATCH_BIAS;
            source &= STYLUS_WINDOW_MASK;
            length = ((uint32_t)second & 0x0fU) + STYLUS_MATCH_MIN_LENGTH;

            for (i = 0U; i < length; ++i) {
                /* A reference may legally point into the still-untouched part
                 * of the ring; the zero fill is part of the format, so this
                 * must NOT be rejected as a back-reference before the start of
                 * output. */
                uint8_t copied = state.window[source];
                source = (source + 1U) & STYLUS_WINDOW_MASK;
                if (!stylus_emit(&state, copied)) return false;
            }
        }
    }

    if (produced) *produced = state.produced;
    if (consumed) *consumed = state.offset;
    return true;
}

bool xx_stylus_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written) {
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!output) return false;
    if (!stylus_run(input, input_size, output, output_size, &produced, NULL)) {
        return false;
    }
    if (written) *written = produced;
    /* Short of the declared size means the caller was told the wrong length;
     * running past it is caught inside stylus_emit(). */
    return produced == output_size;
}

bool xx_stylus_scan_memory(const uint8_t *input, size_t input_size,
                           size_t max_output, size_t *consumed,
                           size_t *produced) {
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (max_output == 0U) return false;
    return stylus_run(input, input_size, NULL, max_output, produced, consumed);
}
