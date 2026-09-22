/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM TERSE (TRSMAIN / AMATERSE): a port of XTERSEDecoder / TerseWalker,
 * keeping its exact ring-recycling arithmetic.  See xx_terse.h for the format.
 */
#include "xxfclib/algo/terse/xx_terse.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define TERSE_NODES 4096U      /* 0x000 .. 0xFFF                            */
#define TERSE_RING_FIRST 0x101 /* first recyclable node                     */
#define TERSE_RING_LAST 0xffe  /* last recyclable node                      */
#define TERSE_MAX_DEPTH 0x1000 /* the reference expansion guard             */
#define TERSE_STACK_SIZE 0x1010 /* one push in, two out per expansion       */

/* The whole decoder state.  It lives in a heap block owned by the call, never
 * in a module-level array: this decoder is called from several threads. */
typedef struct terse_state {
    const uint8_t *input;
    size_t input_size;
    size_t position;
    uint32_t bits;
    int bit_count;
    uint8_t *output;   /* NULL when only measuring                          */
    size_t limit;
    size_t count;
    bool overflow;
    int head;
    uint16_t weights[TERSE_NODES];
    uint16_t next[TERSE_NODES];
    uint16_t prev[TERSE_NODES];
    uint16_t left[TERSE_NODES];
    uint16_t right[TERSE_NODES];
    int stack[TERSE_STACK_SIZE];
} terse_state;

/* The reference sanity probe over the first five 12-bit codes.  Even and odd
 * codes are read by two different helpers, which is why the cursor advances by
 * one byte and then by two: a pair of 12-bit MSB-first codes spans three bytes,
 * so the even code is (b[p] << 4) | (b[p+1] >> 4) and the odd one is
 * ((b[p] & 0x0F) << 8) | b[p+1].  This really is reading codes 0..4, and the
 * rising limits (0x100, 0x100, 0x102, 0x103, 0x104) are the largest symbol the
 * pair tree can possibly have defined by that point. */
static bool terse_probe(const uint8_t *data, size_t size, size_t offset)
{
    static const int limits[5] = {0x100, 0x100, 0x102, 0x103, 0x104};
    size_t position = offset;
    int i;

    if (size < offset + 8U) return false;

    for (i = 0; i < 5; ++i) {
        int value;
        if (i & 1) {
            value = (int)((((uint32_t)(data[position] & 0x0fU)) << 8) +
                          (uint32_t)data[position + 1U]) - 1;
            position += 2U;
        } else {
            value = (int)(((uint32_t)data[position] * 16U) +
                          ((uint32_t)data[position + 1U] >> 4)) - 1;
            position += 1U;
        }
        if (value < 0 || value >= limits[i]) return false;
    }
    return true;
}

bool xx_terse_detect(const uint8_t *input, size_t input_size,
                     size_t *header_size)
{
    bool header_ok = false;

    if (header_size) *header_size = 0U;
    if (!input || input_size < 5U) return false;

    /* 0xA5698901 read little-endian, i.e. the bytes 01 89 69 A5. */
    if (input[0] == 0x01U && input[1] == 0x89U && input[2] == 0x69U &&
        input[3] == 0xa5U) {
        if (header_size) *header_size = 4U;
        return true;
    }
    if (input_size < 12U) return false;

    if (input[0] == 0x05U && input[1] < 2U && (input[2] | input[3]) != 0U &&
        input[4] == 0U && input[5] == 0U && input[6] == 0U && input[7] == 0U &&
        input[8] == 0U && input[9] == 0U && input[10] == 0U &&
        input[11] == 0U) {
        header_ok = true;
    }
    if (!header_ok && input[0] == 0x09U && input[1] < 2U &&
        (input[2] | input[3]) != 0U && input[4] != 0U && input[8] == 0U &&
        input[9] == 0U && input[10] == 0U && input[11] == 0U) {
        header_ok = true;
    }
    if (!header_ok && input[0] == 0x02U && input[1] < 2U &&
        (input[2] | input[3]) != 0U && input[4] != 0U && input[8] == 0U &&
        input[9] == 0U && input[10] == 0U && input[11] == 0U) {
        header_ok = true;
    }
    if (!header_ok) return false;
    if (input_size <= 0x13U) return false;
    if (!terse_probe(input, input_size, 12U)) return false;

    if (header_size) *header_size = 12U;
    return true;
}

static void terse_put(terse_state *state, uint8_t value)
{
    if (state->count >= state->limit) {
        state->overflow = true;
        return;
    }
    if (state->output) state->output[state->count] = value;
    state->count++;
}

/* 12 bits, MSB first, out of a 32-bit MSB-aligned accumulator. */
static bool terse_get_code(terse_state *state, int *code)
{
    while (state->bit_count <= 11) {
        uint32_t byte;
        if (state->position >= state->input_size) return false;
        byte = (uint32_t)state->input[state->position];
        state->position++;
        state->bits = (uint32_t)(state->bits +
                                 (byte << (24 - state->bit_count)));
        state->bit_count += 8;
    }
    *code = (int)(state->bits >> 20);
    state->bits = (uint32_t)(state->bits << 12);
    state->bit_count -= 12;
    return true;
}

/* Pre-order walk of the pair tree.  Pushing RT before LT makes LT pop first. */
static bool terse_expand(terse_state *state, int code)
{
    int depth = 0;
    int top = 0;

    state->stack[top++] = code;
    while (top > 0) {
        int node = state->stack[--top];
        if (node < 0x100) {
            terse_put(state, (uint8_t)node);
            if (state->overflow) return false;
        } else if (node != 0x100) {
            depth++;
            if (depth > TERSE_MAX_DEPTH) return false;
            if ((top + 2) > TERSE_STACK_SIZE) return false;
            if (node < 0 || node >= (int)TERSE_NODES) return false;
            state->stack[top++] = (int)state->right[node];
            state->stack[top++] = (int)state->left[node];
        }
    }
    return true;
}

/* Unlink the node and re-insert it right in front of the head, i.e. at the MRU
 * tail of the circular list. */
static void terse_touch(terse_state *state, int node)
{
    int before = (int)state->prev[node];
    int after = (int)state->next[node];
    int head;
    int tail;

    state->next[before] = (uint16_t)after;
    state->prev[after] = (uint16_t)before;

    head = state->head;
    tail = (int)state->prev[head];
    state->next[tail] = (uint16_t)node;
    state->prev[node] = (uint16_t)tail;
    state->prev[head] = (uint16_t)node;
    state->next[node] = (uint16_t)head;
}

static void terse_bump(terse_state *state, int node, int delta)
{
    if (node > 0x100) {
        /* 16-bit wrap-around is intentional: a zero weight decremented becomes
         * 0xFFFF and the node stops being a recycling candidate. */
        state->weights[node] = (uint16_t)(state->weights[node] + delta);
        terse_touch(state, node);
    }
}

static void terse_init(terse_state *state, const uint8_t *input,
                       size_t input_size, size_t header_size, uint8_t *output,
                       size_t limit)
{
    int i;

    xx_rt_memset(state, 0, sizeof(*state));
    state->input = input;
    state->input_size = input_size;
    state->position = header_size;
    state->output = output;
    state->limit = limit;
    state->head = TERSE_RING_FIRST;

    for (i = TERSE_RING_FIRST; i <= TERSE_RING_LAST; ++i) {
        state->next[i] = (uint16_t)(i + 1);
        state->prev[i] = (uint16_t)(i - 1);
    }
    state->next[TERSE_RING_LAST] = (uint16_t)TERSE_RING_FIRST;
    state->prev[TERSE_RING_FIRST] = (uint16_t)TERSE_RING_LAST;
}

/* The decode loop, shared by both entry points.  With state->output NULL
 * nothing is kept and only the length accumulates; no window is needed because
 * an expansion reads the node tables only, never the bytes already emitted. */
static bool terse_run(terse_state *state)
{
    int code = 0;
    int previous;

    if (!terse_get_code(state, &code)) return false;
    if (code == 0) return true;
    code--;
    if (code >= 0x100) return false;
    terse_put(state, (uint8_t)code);
    if (state->overflow) return false;

    previous = code;

    for (;;) {
        int node;
        int steps = 0;

        if (!terse_get_code(state, &code)) return false;
        if (code == 0) return true;
        code--;
        if (!terse_expand(state, code)) return false;

        while (state->weights[state->head] != 0U) {
            state->head = (int)state->next[state->head];
            steps++;
            if (steps > TERSE_MAX_DEPTH) return false;
        }

        node = state->head;
        terse_bump(state, (int)state->left[node], -1);
        terse_bump(state, (int)state->right[node], -1);
        state->left[node] = (uint16_t)previous;
        state->right[node] = (uint16_t)code;
        terse_bump(state, previous, 1);
        terse_bump(state, code, 1);
        previous = code;
        state->head = (int)state->next[node];
        terse_touch(state, node);
    }
}

/*
 * One core routine for both entry points.
 *
 * DEVIATION FROM THE REFERENCE, deliberate: XTERSEDecoder::measure() and
 * ::decode() both ignore the return value of TerseWalker::run() and keep
 * whatever a truncated stream produced before the cut.  This library's contract
 * forbids reporting a partial decode as success, so a stream that runs out of
 * input before its end code fails here instead.  A stream that terminates
 * cleanly produces byte-for-byte the same output either way.
 */
static bool terse_core(const uint8_t *input, size_t input_size,
                       uint8_t *output, size_t limit, size_t *produced,
                       size_t *consumed)
{
    terse_state *state;
    size_t header_size = 0U;
    bool ok;

    if (produced) *produced = 0U;
    if (consumed) *consumed = 0U;
    /* limit == 0 is legal: a stream whose first code is the end code decodes
     * to nothing, and any byte produced then trips the overflow check. */
    if (!input) return false;
    if (!xx_terse_detect(input, input_size, &header_size)) return false;
    if (input_size <= header_size) return false;

    state = (terse_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return false;
    terse_init(state, input, input_size, header_size, output, limit);

    ok = terse_run(state);
    if (ok) {
        /* Whole bytes still sitting unread in the bit cache were never part of
         * the stream: a caller checking that a container ends exactly at EOF
         * needs the position the decoder actually reached. */
        size_t buffered = (size_t)(state->bit_count / 8);
        if (produced) *produced = state->count;
        if (consumed) {
            *consumed = state->position > buffered ? state->position - buffered
                                                   : 0U;
        }
    }
    xx_mem_free(state);
    return ok;
}

bool xx_terse_decode_memory(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size,
                            size_t *written)
{
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!output) return false;
    if (!terse_core(input, input_size, output, output_size, &produced, NULL)) {
        return false;
    }
    if (produced != output_size) return false;
    if (written) *written = produced;
    return true;
}

bool xx_terse_scan_memory(const uint8_t *input, size_t input_size,
                          size_t max_output, size_t *consumed,
                          size_t *produced)
{
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (max_output == 0U) return false;
    if (max_output > XX_TERSE_MAX_UNCOMPRESSED_SIZE) {
        max_output = XX_TERSE_MAX_UNCOMPRESSED_SIZE;
    }
    return terse_core(input, input_size, NULL, max_output, produced, consumed);
}
