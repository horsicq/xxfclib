/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ChArc method-1 decoder.  Ported one-for-one from the XArchive reference
 * decoder (XArchive/Algos/xcharcdecoder.cpp), which was validated over all
 * 124 members of the ten-carrier reference corpus.  Table layout, table walk,
 * the 16-bit MSB-first bit reader and the model-header order are reproduced
 * exactly; only the state lives in one caller-owned heap block instead of a
 * C++ object, and output goes to a flat caller buffer instead of a QByteArray.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/charc/xx_charc.h"

/* 256 byte contexts, then 0x100 (length / distance high), 0x101 (distance
 * low) and 0x102 (the fallback table borrowed by every mode-0 context). */
#define CHARC_CONTEXT_COUNT 0x103
#define CHARC_LITERAL_CONTEXTS 0x100
#define CHARC_CONTEXT_LENGTH 0x100
#define CHARC_CONTEXT_DISTANCE_LOW 0x101
#define CHARC_CONTEXT_FALLBACK 0x102
/* Per-context tables are cut out of one flat arena, exactly as the reference
 * implementation does; the two budgets below are its own. */
#define CHARC_ARENA_SIZE 0x10000
#define CHARC_TABLE_BUDGET 0x400
#define CHARC_META_BUDGET 0x279
#define CHARC_WINDOW_SIZE 0x10000
#define CHARC_WINDOW_MASK 0xffff
#define CHARC_SYMBOL_COUNT 0x100
/* The reference refuses a declared size above 0x7fffffff; kept so the two
 * decoders agree on every input, not because the port needs a ceiling. */
#define CHARC_MAX_OUTPUT 0x7fffffff
/* A match is at most 0xff + 7 + 1 bytes, so this is the only overshoot the
 * last match of a member can produce.  Deliberate: the reference lets the
 * final match run past the declared length and truncates afterwards. */
#define CHARC_OVERSHOOT_LIMIT 0x200

/* MSB-first out of a 16-bit accumulator topped up one byte at a time.  No
 * request is wider than eight bits, which is why one refill is enough.
 * Reading this as a conventional 32-bit LSB-first reader desynchronises at
 * once - the single-byte refill is load-bearing. */
typedef struct charc_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint16_t buffer;
    int32_t count;
} charc_bits;

typedef struct charc_state_s {
    uint8_t lengths[CHARC_SYMBOL_COUNT];
    uint8_t meta_table[CHARC_META_BUDGET];
    uint8_t context_mode[CHARC_CONTEXT_COUNT];
    uint8_t escape[CHARC_CONTEXT_COUNT];
    uint8_t arena[CHARC_ARENA_SIZE];
    uint8_t window[CHARC_WINDOW_SIZE];
    /* -1 means "this context reads a raw eight-bit byte"; anything else is a
     * byte offset into `arena`. */
    int32_t context_table[CHARC_CONTEXT_COUNT];
    int32_t arena_used;
    int32_t window_position;
} charc_state;

static int32_t charc_read_bits(charc_bits *bits, int32_t nbits) {
    int32_t value;

    if ((nbits < 1) || (nbits > 8)) return -1;

    if (bits->count < nbits) {
        uint32_t byte;
        if (bits->position >= bits->size) return -1;
        byte = (uint32_t)bits->data[bits->position];
        bits->position++;
        bits->buffer = (uint16_t)(bits->buffer + (uint16_t)(byte << (8 - bits->count)));
        bits->count += 8;
    }

    value = (int32_t)((uint32_t)(bits->buffer >> (16 - nbits)) & 0xffU);
    bits->buffer = (uint16_t)(bits->buffer << nbits);
    bits->count -= nbits;

    return value;
}

/* Lay out one table as [count][symbol x count] blocks, one block per code
 * length from zero upwards, and stop as soon as the code space is exhausted.
 * Returns the number of bytes written, or 0 when the budget ran out first.
 * The 16-bit truncations of `available` are the reference's own arithmetic
 * and are kept deliberately. */
static int32_t charc_build_table(const uint8_t *lengths, uint8_t *destination, int32_t capacity) {
    int32_t written;
    int32_t remaining_capacity;
    uint8_t current_length;
    int32_t available;
    int32_t position;

    if (capacity < 1) return 0;

    destination[0] = 0;
    written = 1;
    remaining_capacity = capacity - 1;
    current_length = 0;
    available = 1;
    position = 1;

    for (;;) {
        int32_t count_position = position;
        int32_t index;
        int32_t left;

        current_length = (uint8_t)(current_length + 1);
        available = (int32_t)(int16_t)(available * 2);

        if (remaining_capacity < 1) return 0;
        destination[count_position] = 0;
        position = count_position + 1;
        written++;
        remaining_capacity--;

        index = 0;
        left = CHARC_SYMBOL_COUNT;

        for (;;) {
            uint8_t length = 0;

            while (left >= 1) {
                left--;
                length = lengths[index];
                index++;
                if (length == current_length) break;
            }

            if (length != current_length) break;

            if (remaining_capacity < 1) return 0;
            destination[position] = (uint8_t)(255 - left);
            position++;
            written++;
            remaining_capacity--;
            destination[count_position] = (uint8_t)(destination[count_position] + 1);
            available = (int32_t)(int16_t)(available - 1);
            if (available == 0) return written;
            if (left <= 0) break;
        }
    }
}

/* Walk the blocks laid out above.  `available` is the number of codes still
 * open at the current length and the symbols of that length take the LAST
 * `count` of them.  `budget` is the format's own accounting; `bytes` is the
 * hard end of the buffer and exists only so a corrupt table cannot read past
 * the arena. */
static int32_t charc_decode_symbol(charc_bits *bits, const uint8_t *table, int32_t bytes, int32_t budget) {
    int32_t code = 0;
    int32_t available = 1;
    int32_t offset = 0;
    int32_t remaining = budget;

    for (;;) {
        int32_t count;
        int32_t bit;

        if ((offset < 0) || (offset >= bytes)) return -1;
        count = (int32_t)table[offset];

        if ((available - count) <= code) {
            int32_t index;
            remaining -= (count - available);
            if (remaining < 0) return -1;
            if (remaining < (code + 1)) return -1;
            index = offset + code + (count - available) + 1;
            if ((index <= offset) || (index >= bytes)) return -1;
            return (int32_t)table[index];
        }

        bit = charc_read_bits(bits, 1);
        if (bit < 0) return -1;

        code = (int32_t)(uint16_t)(code * 2 + bit);
        available = (int32_t)(uint16_t)((available - count) * 2);
        remaining -= (count + 1);
        offset += (count + 1);
        if (remaining < 0) return -1;
    }
}

static int32_t charc_decode_context(charc_state *state, charc_bits *bits, int32_t context) {
    int32_t table;

    if ((context < 0) || (context >= CHARC_CONTEXT_COUNT)) return -1;

    table = state->context_table[context];
    if (table < 0) return charc_read_bits(bits, 8);
    if (table >= CHARC_ARENA_SIZE) return -1;

    return charc_decode_symbol(bits, state->arena + table, CHARC_ARENA_SIZE - table, CHARC_TABLE_BUDGET);
}

/* One context's table.  skip_mode2 leaves the length of any symbol whose own
 * context mode is 2 at zero instead of spending a meta symbol on it; the two
 * distance/length contexts are built without it.  That asymmetry is
 * load-bearing - do not "tidy" it into a single rule. */
static bool charc_build_context(charc_state *state, charc_bits *bits, int32_t context, bool skip_mode2) {
    const uint8_t mode = state->context_mode[context];

    if (mode == 3) {
        int32_t i;
        int32_t capacity;
        int32_t written;

        xx_rt_memset(state->lengths, 0, CHARC_SYMBOL_COUNT);

        for (i = 0; i < CHARC_SYMBOL_COUNT; i++) {
            bool decode = true;
            if (skip_mode2 && (state->context_mode[i] == 2)) decode = false;
            if (decode) {
                int32_t value = charc_decode_symbol(bits, state->meta_table, CHARC_META_BUDGET, CHARC_META_BUDGET);
                if (value < 0) return false;
                state->lengths[i] = (uint8_t)value;
            }
        }

        if ((state->arena_used < 0) || (state->arena_used >= CHARC_ARENA_SIZE)) return false;

        capacity = CHARC_ARENA_SIZE - state->arena_used;
        if (capacity > CHARC_TABLE_BUDGET) capacity = CHARC_TABLE_BUDGET;

        written = charc_build_table(state->lengths, state->arena + state->arena_used, capacity);
        if (written <= 0) return false;

        state->context_table[context] = state->arena_used;
        state->arena_used += written;
    } else if (mode != 0) {
        state->context_table[context] = -1;
    }

    return true;
}

static bool charc_init_model(charc_state *state, charc_bits *bits) {
    int32_t all_literal;
    int32_t has_escape_table;
    int32_t i;

    all_literal = charc_read_bits(bits, 1);
    if (all_literal < 0) return false;

    if (all_literal == 0) {
        xx_rt_memset(state->lengths, 0, CHARC_SYMBOL_COUNT);

        for (i = 0; i < 4; i++) {
            int32_t value = charc_read_bits(bits, 3);
            if (value < 0) return false;
            state->lengths[i] = (uint8_t)value;
        }

        if (charc_build_table(state->lengths, state->meta_table, CHARC_META_BUDGET) <= 0) return false;

        for (i = 0; i < CHARC_CONTEXT_COUNT; i++) {
            int32_t value = charc_decode_symbol(bits, state->meta_table, CHARC_META_BUDGET, CHARC_META_BUDGET);
            if (value < 0) return false;
            state->context_mode[i] = (uint8_t)value;
        }
    } else {
        xx_rt_memset(state->context_mode, 1, CHARC_CONTEXT_COUNT);
    }

    has_escape_table = charc_read_bits(bits, 1);
    if (has_escape_table < 0) return false;

    if (has_escape_table != 0) {
        int32_t default_escape = charc_read_bits(bits, 8);
        if (default_escape < 0) return false;
        xx_rt_memset(state->escape, (int)default_escape, CHARC_LITERAL_CONTEXTS);

        for (;;) {
            int32_t more;
            int32_t value;
            int32_t index;

            more = charc_read_bits(bits, 1);
            if (more < 0) return false;
            if (more == 0) break;

            value = charc_read_bits(bits, 8);
            if (value < 0) return false;
            index = charc_read_bits(bits, 8);
            if (index < 0) return false;
            state->escape[index] = (uint8_t)value;
        }
    }

    if (all_literal == 0) {
        int32_t highest = charc_read_bits(bits, 5);
        if (highest < 0) return false;

        /* Deliberate: `lengths` is NOT cleared before this pass.  Entries
         * above `highest` keep whatever the meta-length pass above left
         * there - for highest < 3 that means the 3-bit values of symbols
         * 0..3 survive into the rebuilt meta table.  The reference does the
         * same; clearing here changes the table. */
        for (i = 0; i <= highest; i++) {
            int32_t value = charc_read_bits(bits, 4);
            if (value < 0) return false;
            state->lengths[i] = (uint8_t)value;
        }

        if (charc_build_table(state->lengths, state->meta_table, CHARC_META_BUDGET) <= 0) return false;
    }

    for (i = 0; i < CHARC_LITERAL_CONTEXTS; i++) {
        if (!charc_build_context(state, bits, i, true)) return false;
    }
    for (i = CHARC_CONTEXT_LENGTH; i < CHARC_CONTEXT_FALLBACK; i++) {
        if (!charc_build_context(state, bits, i, false)) return false;
    }
    if (!charc_build_context(state, bits, CHARC_CONTEXT_FALLBACK, true)) return false;

    for (i = 0; i < CHARC_CONTEXT_FALLBACK; i++) {
        if (state->context_mode[i] == 0) state->context_table[i] = state->context_table[CHARC_CONTEXT_FALLBACK];
    }

    return true;
}

/* Every produced byte always enters the ring; it reaches the caller's buffer
 * only while there is room.  The reference appends to a growing QByteArray
 * and truncates the final overshoot afterwards, so dropping the overshoot
 * here is output-equivalent - the loop that produced it ends immediately. */
static void charc_emit(charc_state *state, uint8_t *output, size_t output_size, size_t *produced, uint8_t byte) {
    if (*produced < output_size) output[*produced] = byte;
    (*produced)++;
    state->window[state->window_position] = byte;
    state->window_position = (state->window_position + 1) & CHARC_WINDOW_MASK;
}

static bool charc_decode_stream(charc_state *state, const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *produced) {
    charc_bits bits;
    int32_t length_bias;
    int32_t first_byte;
    int32_t context;

    bits.data = input;
    bits.size = input_size;
    bits.position = 0;
    bits.buffer = 0;
    bits.count = 0;

    if (!charc_init_model(state, &bits)) return false;

    length_bias = charc_read_bits(&bits, 3);
    if (length_bias < 0) return false;

    first_byte = charc_read_bits(&bits, 8);
    if (first_byte < 0) return false;

    charc_emit(state, output, output_size, produced, (uint8_t)first_byte);
    context = first_byte;

    while (*produced < output_size) {
        int32_t symbol;
        int32_t length_code;
        int32_t length;
        int32_t distance_low;
        int32_t distance_high;
        int32_t distance;

        symbol = charc_decode_context(state, &bits, context);
        if (symbol < 0) return false;

        if (symbol != (int32_t)state->escape[context & 0xff]) {
            charc_emit(state, output, output_size, produced, (uint8_t)symbol);
            context = symbol;
            continue;
        }

        length_code = charc_decode_context(state, &bits, CHARC_CONTEXT_LENGTH);
        if (length_code < 0) return false;

        if (length_code == 0) {
            /* Length code zero means "the escape byte itself, literally". */
            charc_emit(state, output, output_size, produced, (uint8_t)symbol);
            context = symbol;
            continue;
        }

        length = (int32_t)(int16_t)(length_code + length_bias + 1);

        distance_low = charc_decode_context(state, &bits, CHARC_CONTEXT_DISTANCE_LOW);
        if (distance_low < 0) return false;
        distance_high = charc_decode_context(state, &bits, CHARC_CONTEXT_LENGTH);
        if (distance_high < 0) return false;

        distance = (distance_low + distance_high * 256) & CHARC_WINDOW_MASK;

        /* No "unused window" guard: the ring is a flat 64 KiB indexed modulo
         * 0x10000 and a distance may legitimately reach back into bytes this
         * member never wrote (they read as zero).  The reference tolerates
         * this and real members rely on it, so it is not treated as a
         * malformed back-reference. */
        while (length > 0) {
            uint8_t byte = state->window[(state->window_position - distance) & CHARC_WINDOW_MASK];
            charc_emit(state, output, output_size, produced, byte);
            context = (int32_t)byte;
            length--;
            if (*produced > (output_size + (size_t)CHARC_OVERSHOOT_LIMIT)) return false;
        }
    }

    return true;
}

bool xx_charc_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written) {
    charc_state *state;
    size_t produced = 0;
    bool decoded;
    int32_t i;

    if (written) *written = 0;
    if (output_size > (size_t)CHARC_MAX_OUTPUT) return false;
    if (output_size == 0) return true;
    if (!input || !output) return false;
    if (input_size == 0) return false;

    state = (charc_state *)xx_mem_alloc(sizeof(charc_state));
    if (!state) return false;

    xx_rt_memset(state, 0, sizeof(charc_state));
    for (i = 0; i < CHARC_CONTEXT_COUNT; i++) state->context_table[i] = -1;

    decoded = charc_decode_stream(state, input, input_size, output, output_size, &produced);

    xx_mem_free(state);

    if (!decoded) return false;
    if (produced < output_size) return false;

    if (written) *written = output_size;

    return true;
}
