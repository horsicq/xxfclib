/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Corel/LEAD Technologies "LTEC" installer archive codec, ported from
 * XArchive's XCorelLtecDecoder (Algos/xcorelltecdecoder.cpp).  The algorithm,
 * the constants and every acceptance/rejection decision follow that file; the
 * deliberate oddities are called out where they occur.
 */
#include "xxfclib/algo/corelltec/xx_corelltec.h"

#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define LTEC_NC 510           /* 256 literals + 254 match lengths          */
#define LTEC_CBIT 9           /* width of the literal-table symbol count   */
#define LTEC_NT 19            /* pre-table alphabet                        */
#define LTEC_TBIT 5           /* width of the pre-table symbol count       */
#define LTEC_NP 24            /* position alphabet ceiling                 */
#define LTEC_PBIT 5           /* width of the position-table symbol count  */
#define LTEC_THRESHOLD 3      /* shortest encodable match                  */
#define LTEC_PT_SPECIAL 3     /* pre-table index carrying the 2-bit run    */
#define LTEC_MAX_CODE_LENGTH 16
/* Only the two uninformative bits-of-nothing at the head of a block. */
#define LTEC_PRELUDE_BITS 16
/* The last symbol of a block can need a few bits the block's own byte range
 * does not cover: the encoder starts the next block on the byte that holds
 * them (that overlap is exactly why every block opens with two stale bytes).
 * Two bytes is the measured worst case over all 462 corpus blocks; the reader
 * serves zeroes inside this margin and fails past it, so a truncated archive
 * still cannot be decoded into plausible-looking output.  DELIBERATE. */
#define LTEC_TAIL_SLACK_BITS 16

/* ---------------------------------------------------------------------- */
/* MSB-first bit reader over the packed block                              */
/* ---------------------------------------------------------------------- */

typedef struct ltec_bits_s {
    const uint8_t *data;
    uint64_t size;      /* bytes                                          */
    uint64_t bit_count; /* size * 8                                       */
    uint64_t bit_pos;
} ltec_bits;

static void ltec_bits_init(ltec_bits *reader, const uint8_t *data,
                           size_t size) {
    reader->data = data;
    reader->size = (uint64_t)size;
    reader->bit_count = (uint64_t)size * 8U;
    reader->bit_pos = 0U;
}

/* The reference guard is `bitPos > bitCount + SLACK - count`, which is the
 * same as the unsigned-safe form below. */
static bool ltec_read_bits(ltec_bits *reader, uint32_t count,
                           uint32_t *value) {
    uint32_t result = 0U;
    uint32_t i;

    if (count > 24U) return false;
    if ((reader->bit_pos + (uint64_t)count) >
        (reader->bit_count + (uint64_t)LTEC_TAIL_SLACK_BITS)) {
        return false;
    }
    for (i = 0U; i < count; ++i) {
        uint32_t bit = 0U;
        uint64_t byte_index = reader->bit_pos >> 3;
        if (byte_index < reader->size) {
            bit = (uint32_t)((reader->data[(size_t)byte_index] >>
                              (7U - (unsigned)(reader->bit_pos & 7U))) &
                             1U);
        }
        result = (result << 1U) | bit;
        ++reader->bit_pos;
    }
    *value = result;
    return true;
}

static bool ltec_skip_bits(ltec_bits *reader, uint32_t count) {
    uint32_t dummy = 0U;
    while (count > 24U) {
        if (!ltec_read_bits(reader, 24U, &dummy)) return false;
        count -= 24U;
    }
    return ltec_read_bits(reader, count, &dummy);
}

/* ---------------------------------------------------------------------- */
/* Canonical Huffman table                                                 */
/* ---------------------------------------------------------------------- */

/* `constant` covers LHA's "declared count is zero" shape, where a single
 * symbol owns the whole alphabet and costs no bits. */
typedef struct ltec_tree_s {
    uint32_t count[LTEC_MAX_CODE_LENGTH + 1];
    uint32_t first_code[LTEC_MAX_CODE_LENGTH + 1];
    uint32_t first_index[LTEC_MAX_CODE_LENGTH + 1];
    uint16_t symbols[LTEC_NC]; /* NC is the largest alphabet in use        */
    uint32_t symbol_count;
    uint32_t max_length;
    bool constant;
    uint32_t constant_symbol;
} ltec_tree;

/* All three trees plus the scratch the table readers need, so nothing sits in
 * module-level state and the stack stays small. */
typedef struct ltec_state_s {
    ltec_tree pre_tree;
    ltec_tree literal_tree;
    ltec_tree position_tree;
    uint8_t lengths[LTEC_NC];
    uint32_t next_index[LTEC_MAX_CODE_LENGTH + 1];
} ltec_state;

static void ltec_reset_tree(ltec_tree *tree) {
    uint32_t i;
    for (i = 0U; i <= (uint32_t)LTEC_MAX_CODE_LENGTH; ++i) {
        tree->count[i] = 0U;
        tree->first_code[i] = 0U;
        tree->first_index[i] = 0U;
    }
    tree->symbol_count = 0U;
    tree->max_length = 0U;
    tree->constant = false;
    tree->constant_symbol = 0U;
}

/* Every table this format emits is Kraft-complete (checked over all 462
 * blocks of the reference corpus).  Rejecting incomplete and over-subscribed
 * tables therefore costs nothing on genuine input and turns a mis-parse into
 * an immediate failure instead of plausible-looking wrong plaintext. */
static bool ltec_build_tree(ltec_state *state, uint32_t alphabet,
                            ltec_tree *tree) {
    uint32_t max_length = 0U;
    int64_t left;
    uint32_t code;
    uint32_t index;
    uint32_t length;
    uint32_t i;

    ltec_reset_tree(tree);

    for (i = 0U; i < alphabet; ++i) {
        uint32_t value = state->lengths[i];
        if (value > (uint32_t)LTEC_MAX_CODE_LENGTH) return false;
        if (value > 0U) {
            ++tree->count[value];
            if (value > max_length) max_length = value;
        }
    }
    if (max_length == 0U) return false;
    tree->max_length = max_length;

    left = 1;
    code = 0U;
    index = 0U;
    for (length = 1U; length <= max_length; ++length) {
        left = (left << 1) - (int64_t)tree->count[length];
        if (left < 0) return false;
        tree->first_code[length] = code;
        tree->first_index[length] = index;
        code = (code + tree->count[length]) << 1U;
        index += tree->count[length];
    }
    if (left != 0) return false;

    tree->symbol_count = index;
    for (length = 1U; length <= max_length; ++length) {
        state->next_index[length] = tree->first_index[length];
    }
    for (i = 0U; i < alphabet; ++i) {
        uint32_t value = state->lengths[i];
        if (value > 0U) {
            tree->symbols[state->next_index[value]++] = (uint16_t)i;
        }
    }
    return true;
}

static bool ltec_decode_symbol(ltec_bits *reader, const ltec_tree *tree,
                               uint32_t *symbol) {
    uint32_t code = 0U;
    uint32_t length;

    if (tree->constant) {
        *symbol = tree->constant_symbol;
        return true;
    }
    for (length = 1U; length <= tree->max_length; ++length) {
        uint32_t bit = 0U;
        if (!ltec_read_bits(reader, 1U, &bit)) return false;
        code = (code << 1U) | bit;
        if (tree->count[length] && (code >= tree->first_code[length]) &&
            ((code - tree->first_code[length]) < tree->count[length])) {
            *symbol = tree->symbols[tree->first_index[length] +
                                    (code - tree->first_code[length])];
            return true;
        }
    }
    return false;
}

/* Pre-table / position-table reader.  The 3-bit length escape at value 7 is
 * extended by counting the following 1-bits AND consuming the terminating 0
 * bit (LHA's fillbuf(c - 3)).  Dropping that consume still decodes the short
 * tables perfectly while corrupting the long ones, so it is called out here.
 *
 * `special_index` is -1 for the position table, where the two-bit zero run
 * does not exist; `i` is only ever compared after it has been incremented, so
 * -1 can never match.  DELIBERATE, matching the reference. */
static bool ltec_read_pt_len(ltec_bits *reader, ltec_state *state,
                             uint32_t alphabet, uint32_t count_bits,
                             int32_t special_index, ltec_tree *tree) {
    uint32_t number = 0U;
    uint32_t i;

    ltec_reset_tree(tree);

    if (!ltec_read_bits(reader, count_bits, &number)) return false;
    if (number == 0U) {
        uint32_t symbol = 0U;
        if (!ltec_read_bits(reader, count_bits, &symbol) ||
            (symbol >= alphabet)) {
            return false;
        }
        tree->constant = true;
        tree->constant_symbol = symbol;
        return true;
    }
    if (number > alphabet) return false;

    for (i = 0U; i < alphabet; ++i) state->lengths[i] = 0U;

    i = 0U;
    while (i < number) {
        uint32_t length = 0U;
        if (!ltec_read_bits(reader, 3U, &length)) return false;
        if (length == 7U) {
            for (;;) {
                uint32_t bit = 0U;
                if (!ltec_read_bits(reader, 1U, &bit)) return false;
                if (!bit) break;
                ++length;
                if (length > (uint32_t)LTEC_MAX_CODE_LENGTH) return false;
            }
        }
        state->lengths[i++] = (uint8_t)length;
        if ((int32_t)i == special_index) {
            uint32_t zero_run = 0U;
            if (!ltec_read_bits(reader, 2U, &zero_run)) return false;
            /* Note the bound: the run is clipped against the ALPHABET here,
             * not against the declared count the way ltec_read_c_len clips
             * it.  That asymmetry is in the reference and is load bearing. */
            while ((zero_run > 0U) && (i < alphabet)) {
                state->lengths[i++] = 0U;
                --zero_run;
            }
        }
    }
    return ltec_build_tree(state, alphabet, tree);
}

static bool ltec_read_c_len(ltec_bits *reader, ltec_state *state,
                            ltec_tree *tree) {
    uint32_t number = 0U;
    uint32_t i;

    ltec_reset_tree(tree);

    if (!ltec_read_bits(reader, (uint32_t)LTEC_CBIT, &number)) return false;
    if (number == 0U) {
        uint32_t symbol = 0U;
        if (!ltec_read_bits(reader, (uint32_t)LTEC_CBIT, &symbol) ||
            (symbol >= (uint32_t)LTEC_NC)) {
            return false;
        }
        tree->constant = true;
        tree->constant_symbol = symbol;
        return true;
    }
    if (number > (uint32_t)LTEC_NC) return false;

    for (i = 0U; i < (uint32_t)LTEC_NC; ++i) state->lengths[i] = 0U;

    i = 0U;
    while (i < number) {
        uint32_t symbol = 0U;
        if (!ltec_decode_symbol(reader, &state->pre_tree, &symbol)) {
            return false;
        }
        if (symbol <= 2U) {
            uint32_t zero_run = 0U;
            if (symbol == 0U) {
                zero_run = 1U;
            } else if (symbol == 1U) {
                if (!ltec_read_bits(reader, 4U, &zero_run)) return false;
                zero_run += 3U;
            } else {
                if (!ltec_read_bits(reader, (uint32_t)LTEC_CBIT, &zero_run)) {
                    return false;
                }
                zero_run += 20U;
            }
            /* The run may be declared longer than the count leaves room for;
             * the surplus lands on entries the tail fill would zero anyway.
             * Clipped against the declared COUNT, unlike ltec_read_pt_len. */
            while ((zero_run > 0U) && (i < number)) {
                state->lengths[i++] = 0U;
                --zero_run;
            }
        } else {
            if ((symbol - 2U) > (uint32_t)LTEC_MAX_CODE_LENGTH) return false;
            state->lengths[i++] = (uint8_t)(symbol - 2U);
        }
    }
    return ltec_build_tree(state, (uint32_t)LTEC_NC, tree);
}

/* Decodes a solid block's plaintext up to `stop_size` bytes into `out`.  The
 * window is the plaintext produced so far - a match may reach back to the
 * block's very first byte and never before it, which is what makes a flat
 * buffer both correct and stricter than an LHA ring window pre-filled with
 * spaces.  `out` must hold `stop_size` bytes. */
static bool ltec_decode_block(const uint8_t *input, size_t input_size,
                              uint8_t *out, size_t stop_size,
                              ltec_state *state) {
    ltec_bits reader;
    size_t produced = 0U;
    uint32_t block_remaining = 0U;

    if (stop_size == 0U) return true;
    if (input_size == 0U) return false;

    ltec_bits_init(&reader, input, input_size);
    if (!ltec_skip_bits(&reader, (uint32_t)LTEC_PRELUDE_BITS)) return false;

    ltec_reset_tree(&state->pre_tree);
    ltec_reset_tree(&state->literal_tree);
    ltec_reset_tree(&state->position_tree);

    while (produced < stop_size) {
        uint32_t symbol = 0U;

        if (block_remaining == 0U) {
            /* A block restates all three tables in-stream after each run of
             * symbols; the 16-bit count is how many symbols the run holds. */
            if (!ltec_read_bits(&reader, 16U, &block_remaining) ||
                (block_remaining == 0U)) {
                return false;
            }
            if (!ltec_read_pt_len(&reader, state, (uint32_t)LTEC_NT,
                                  (uint32_t)LTEC_TBIT, LTEC_PT_SPECIAL,
                                  &state->pre_tree) ||
                !ltec_read_c_len(&reader, state, &state->literal_tree) ||
                !ltec_read_pt_len(&reader, state, (uint32_t)LTEC_NP,
                                  (uint32_t)LTEC_PBIT, -1,
                                  &state->position_tree)) {
                return false;
            }
        }
        --block_remaining;

        if (!ltec_decode_symbol(&reader, &state->literal_tree, &symbol)) {
            return false;
        }
        if (symbol < 256U) {
            out[produced++] = (uint8_t)symbol;
            continue;
        }
        if (symbol >= (uint32_t)LTEC_NC) return false;

        {
            uint32_t match_length =
                (symbol - 256U) + (uint32_t)LTEC_THRESHOLD;
            uint32_t position_symbol = 0U;
            uint32_t distance = 0U;
            size_t source;
            uint32_t i;

            if (!ltec_decode_symbol(&reader, &state->position_tree,
                                    &position_symbol) ||
                (position_symbol >= (uint32_t)LTEC_NP)) {
                return false;
            }
            if (position_symbol > 0U) {
                uint32_t extra = 0U;
                if (!ltec_read_bits(&reader, position_symbol - 1U, &extra)) {
                    return false;
                }
                distance = (1U << (position_symbol - 1U)) + extra;
            }
            ++distance;

            /* A back-reference before the start of the block's plaintext is a
             * malformed stream, never a wrap: the reference fails here too. */
            if ((uint64_t)distance > (uint64_t)produced) return false;
            source = produced - (size_t)distance;
            /* Overlapping matches are normal here, so the copy has to be
             * byte-by-byte: a block move would read bytes this run has not
             * written yet. */
            for (i = 0U; i < match_length; ++i) {
                out[produced] = out[source + i];
                ++produced;
                if (produced >= stop_size) break;
            }
        }
    }

    return produced >= stop_size;
}

bool xx_corelltec_decode_memory(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size,
                                size_t *written) {
    ltec_state *state;
    bool result;

    if (written) *written = 0U;
    if (!input && (input_size != 0U)) return false;
    if (!output && (output_size != 0U)) return false;
    if (output_size == 0U) return true; /* as in the reference */

    state = (ltec_state *)xx_mem_alloc(sizeof(ltec_state));
    if (!state) return false;

    result = ltec_decode_block(input, input_size, output, output_size, state);
    xx_mem_free(state);

    if (!result) return false;
    if (written) *written = output_size;
    return true;
}

bool xx_corelltec_decode_member(const uint8_t *input, size_t input_size,
                                size_t offset_in_block, uint8_t *output,
                                size_t output_size, size_t *written) {
    ltec_state *state;
    uint8_t *block;
    size_t stop_size;
    bool result;

    if (written) *written = 0U;
    if (!input && (input_size != 0U)) return false;
    if (!output && (output_size != 0U)) return false;
    if (offset_in_block > (SIZE_MAX - output_size)) return false;
    if (output_size == 0U) return true;
    if (offset_in_block == 0U) {
        return xx_corelltec_decode_memory(input, input_size, output,
                                          output_size, written);
    }

    stop_size = offset_in_block + output_size;
    state = (ltec_state *)xx_mem_alloc(sizeof(ltec_state));
    block = (uint8_t *)xx_mem_alloc(stop_size);
    if (!state || !block) {
        xx_mem_free(state);
        xx_mem_free(block);
        return false;
    }

    result = ltec_decode_block(input, input_size, block, stop_size, state);
    if (result) xx_rt_memcpy(output, block + offset_in_block, output_size);

    xx_mem_free(state);
    xx_mem_free(block);

    if (!result) return false;
    if (written) *written = output_size;
    return true;
}
