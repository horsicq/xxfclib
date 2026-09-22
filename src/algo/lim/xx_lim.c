/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LIM archive method 1 decoder.  Ported one-for-one from the XArchive
 * reference decoder (XArchive/Algos/xlimdecoder.cpp).
 *
 * DELIBERATE, DO NOT "FIX":
 *
 *  - THE STREAM BIT IS THE COMPLEMENT OF THE CODE BIT.  Every bit pulled while
 *    walking a Huffman code is inverted (1 - bit) before it is appended to the
 *    code being matched.  Reading it the ordinary way still decodes a few
 *    hundred plausible bytes before failing, which reads as a table bug.
 *
 *  - THE WINDOW CARRIES A PARALLEL MATCH-LENGTH HISTORY.  The real length of a
 *    match is the table length PLUS history[source], and four more if that sum
 *    overflows a byte.  After the copy the history is cleared across the
 *    destination run and a descending ramp is written at the source.  When the
 *    resulting length is below 3 the ramp counter (length - 3) & 0xff wraps to
 *    a large value and the ramp runs ~253 positions; the reference does this
 *    and it is reproduced here.
 *
 *  - THE WINDOW IS A ZERO-FILLED 32 KiB RING.  A distance reaching back before
 *    the start of output reads those zeroes instead of failing; the reference
 *    tolerates it (there is no output-position check at all), so this port
 *    tolerates it too.  Every window index is still masked to 0x7fff, so no
 *    out-of-bounds access is possible.
 *
 *  - LITERALS ARE NOT LENGTH-CHECKED.  Only the match path tests the produced
 *    size against the declared plaintext length, so a stream may run past that
 *    length; the reference then truncates.  This port keeps decoding (the ring
 *    state still matters) but writes to the caller's buffer only while inside
 *    it, and succeeds only when the declared length was reached exactly.
 *
 *  - A MALFORMED TABLE OR AN EXHAUSTED STREAM ENDS THE STREAM, IT DOES NOT
 *    ABORT.  The reference `break`s out and lets the final length comparison
 *    decide, so a truncated member that happens to hold exactly the declared
 *    number of bytes is a success.  Same here.
 *
 * DEVIATION (provably output-equivalent): the reference stores canonical codes
 * in a QHash keyed by (length << 32) | code and probes that hash once per bit.
 * This port stores, per code length, the first code and the run of symbols that
 * own the consecutive codes from it.  Codes within one length are assigned by a
 * counter incremented once per symbol, so they are distinct and contiguous, and
 * keys never collide across lengths; membership in the hash and membership in
 * the run are therefore the same test, and the symbol returned is the same.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/lim/xx_lim.h"

#define LIM_WINDOW 0x8000
#define LIM_WINDOW_MASK 0x7fff
#define LIM_MAIN_SYMBOLS 0x129 /* 256 literals + 41 match codes */
#define LIM_DIST_SYMBOLS 0x1d
#define LIM_LENGTH_SYMBOLS 20
#define LIM_SHORT_CODES 13
#define LIM_MAX_CODE_LENGTH 32

static const uint16_t g_lim_code[41] = {0,    1,    2,    4,    8,    16,   32,   64,   128,  256,  512,
                                        1024, 2048, 4,    5,    6,    7,    8,    9,    10,   11,   268,
                                        270,  272,  274,  532,  536,  540,  544,  804,  812,  820,  828,
                                        1092, 1108, 1124, 1140, 1412, 1444, 1476, 1508};

static const uint8_t g_lim_distance_extra[LIM_DIST_SYMBOLS] = {0, 0, 1, 1, 1, 2,  2,  3,  3,  4,
                                                               4, 5, 5, 6, 6, 7,  7,  8,  8,  9,
                                                               9, 10, 10, 11, 11, 12, 12, 13, 13};

static const uint16_t g_lim_distance_base[LIM_DIST_SYMBOLS] = {
    0,    1,    2,     4,     6,     8,     12,    16,    24,   32,   48,    64,    96,   128,  192,
    256,  384,  512,   768,   1024,  1536,  2048,  3072,  4096, 6144, 8192,  12288, 16384, 24576};

static const uint8_t g_lim_default_distance_lengths[LIM_DIST_SYMBOLS] = {4, 6, 6, 5, 5, 5, 5, 5, 5, 5,
                                                                         5, 5, 5, 5, 5, 5, 5, 4, 4, 4,
                                                                         5, 5, 5, 5, 5, 5, 5, 5, 5};

typedef struct lim_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position; /* may reach size + 1; past that the stream is exhausted */
    uint32_t accumulator;
    int32_t count;
} lim_bits;

typedef struct lim_huffman_s {
    int32_t max_length;
    uint16_t count[LIM_MAX_CODE_LENGTH + 1];
    uint32_t first_code[LIM_MAX_CODE_LENGTH + 1];
    uint16_t offset[LIM_MAX_CODE_LENGTH + 1];
    uint16_t symbols[LIM_MAIN_SYMBOLS];
} lim_huffman;

typedef struct lim_state_s {
    lim_huffman main_tree;
    lim_huffman distance_tree;
    lim_huffman length_tree;
    uint8_t window[LIM_WINDOW];
    uint8_t history[LIM_WINDOW];
    uint8_t main_lengths[LIM_MAIN_SYMBOLS];
    uint8_t length_lengths[LIM_LENGTH_SYMBOLS];
    uint8_t distance_lengths[LIM_DIST_SYMBOLS];
} lim_state;

/* -1 means the stream is exhausted.  The reference allows exactly one virtual
 * zero byte at position == size before it gives up; reproduced verbatim. */
static int32_t lim_get(lim_bits *bits, int32_t nbits) {
    int32_t value;

    if (nbits == 0) return 0;

    while (bits->count < nbits) {
        uint32_t byte = 0U;

        if (bits->position > bits->size) return -1;
        if (bits->position < bits->size) byte = bits->data[bits->position];
        ++bits->position;
        /* nbits never exceeds 14 in this format, so count < 14 here and the
         * shift stays inside 0..24. */
        bits->accumulator = (bits->accumulator + (byte << (24 - bits->count))) & 0xffffffffU;
        bits->count += 8;
    }

    value = (int32_t)(bits->accumulator >> (32 - nbits));
    bits->accumulator = (bits->accumulator << nbits) & 0xffffffffU;
    bits->count -= nbits;

    return value;
}

static bool lim_huffman_build(lim_huffman *tree, const uint8_t *lengths, int32_t count) {
    int32_t i;
    int32_t length;
    uint32_t code;
    uint16_t next[LIM_MAX_CODE_LENGTH + 1];
    uint16_t running;

    tree->max_length = 0;

    for (i = 0; i < count; ++i) {
        if (lengths[i] > LIM_MAX_CODE_LENGTH) return false;
        if ((int32_t)lengths[i] > tree->max_length) tree->max_length = (int32_t)lengths[i];
    }

    for (length = 0; length <= LIM_MAX_CODE_LENGTH; ++length) {
        tree->count[length] = 0;
        tree->first_code[length] = 0;
        tree->offset[length] = 0;
        next[length] = 0;
    }

    for (i = 0; i < count; ++i) {
        if (lengths[i] != 0) ++tree->count[lengths[i]];
    }

    /* Same walk as the reference: the code counter runs over the lengths in
     * ascending order, one increment per symbol, and is shifted left between
     * lengths.  Symbols within a length are visited in ascending order. */
    code = 0U;
    running = 0;
    for (length = 1; length <= tree->max_length; ++length) {
        tree->first_code[length] = code;
        tree->offset[length] = running;
        code = (code + (uint32_t)tree->count[length]) << 1;
        running = (uint16_t)(running + tree->count[length]);
        next[length] = tree->offset[length];
    }

    for (i = 0; i < count; ++i) {
        if (lengths[i] != 0) {
            tree->symbols[next[lengths[i]]] = (uint16_t)i;
            ++next[lengths[i]];
        }
    }

    return true;
}

static int32_t lim_huffman_decode(const lim_huffman *tree, lim_bits *bits) {
    uint32_t code = 0U;
    int32_t length;

    for (length = 1; length <= tree->max_length; ++length) {
        const int32_t bit = lim_get(bits, 1);
        uint32_t delta;

        if (bit < 0) return -1;
        /* The stream bit is the complement of the code bit -- deliberate. */
        code = (code << 1) | (uint32_t)(1 - bit);

        if (tree->count[length] != 0) {
            delta = code - tree->first_code[length];
            if (delta < (uint32_t)tree->count[length]) {
                return (int32_t)tree->symbols[tree->offset[length] + delta];
            }
        }
    }

    return -1;
}

/* The variable-length encoding the code-length table uses. */
static int32_t lim_read_length_code(lim_bits *bits) {
    int32_t value = lim_get(bits, 2);
    int32_t bit;

    if (value < 0) return -1;
    if (value == 1) return 3;

    bit = lim_get(bits, 1);
    if (bit < 0) return -1;
    value = value * 2 + bit;

    if (value == 1) return 2;

    if (value == 7) {
        value = 6;
        for (;;) {
            const int32_t next = lim_get(bits, 1);
            if (next < 0) return -1;
            ++value;
            if (next != 0) break;
        }
        if (value == 0x0d) return 1;
        if (value > 0x0d) return value - 1;
    }

    return value;
}

static bool lim_read_tables(lim_bits *bits, lim_state *state) {
    const int32_t start = lim_get(bits, 3);
    int32_t count = lim_get(bits, 4);
    int32_t value;
    int32_t index;
    int32_t k;
    int32_t at;
    int32_t flag;

    if ((start < 0) || (count < 0)) return false;

    xx_rt_memset(state->length_lengths, 0, sizeof(state->length_lengths));

    value = lim_read_length_code(bits);
    if (value < 0) return false;
    state->length_lengths[0] = (uint8_t)value;

    index = start;
    while (count != 0) {
        ++index;
        value = lim_read_length_code(bits);
        if ((value < 0) || (index >= LIM_LENGTH_SYMBOLS)) return false;
        state->length_lengths[index] = (uint8_t)value;
        --count;
    }

    for (k = 17; k <= 19; ++k) {
        value = lim_read_length_code(bits);
        if (value < 0) return false;
        state->length_lengths[k] = (uint8_t)value;
    }

    if (!lim_huffman_build(&state->length_tree, state->length_lengths, LIM_LENGTH_SYMBOLS)) return false;

    xx_rt_memset(state->main_lengths, 0, sizeof(state->main_lengths));

    at = 0;
    while (at < LIM_MAIN_SYMBOLS) {
        const int32_t symbol = lim_huffman_decode(&state->length_tree, bits);
        int32_t extra_bits;
        int32_t base;
        int32_t extra;
        uint8_t previous;

        if (symbol < 0) return false;

        if (symbol < 0x11) {
            state->main_lengths[at] = (uint8_t)symbol;
            ++at;
            continue;
        }

        extra_bits = 7;
        base = 0x17;
        if (symbol == 0x11) {
            extra_bits = 2;
            base = 3;
        } else if (symbol == 0x12) {
            extra_bits = 4;
            base = 7;
        }

        extra = lim_get(bits, extra_bits);
        if ((extra < 0) || (at == 0)) return false;

        previous = state->main_lengths[at - 1];
        for (k = 0; k < (base + extra); ++k) {
            if (at >= LIM_MAIN_SYMBOLS) return false;
            state->main_lengths[at] = previous;
            ++at;
        }
    }

    if (!lim_huffman_build(&state->main_tree, state->main_lengths, LIM_MAIN_SYMBOLS)) return false;

    flag = lim_get(bits, 1);
    if (flag < 0) return false;

    if (flag == 0) {
        int32_t how;
        int32_t i;

        xx_rt_memset(state->distance_lengths, 0, sizeof(state->distance_lengths));

        how = lim_get(bits, 5);
        if (how < 0) return false;

        for (i = 0; i < how; ++i) {
            const int32_t bit = lim_get(bits, 1);
            int32_t length = 0;

            if (bit < 0) return false;

            if (bit == 0) {
                const int32_t small = lim_get(bits, 1);
                if (small < 0) return false;
                length = small + 4;
            } else {
                length = lim_read_length_code(bits);
                if (length < 0) return false;
                if (length > 3) length += 2;
            }

            if (i >= LIM_DIST_SYMBOLS) return false;
            state->distance_lengths[i] = (uint8_t)length;
        }
    } else {
        xx_rt_memcpy(state->distance_lengths, g_lim_default_distance_lengths, sizeof(state->distance_lengths));
    }

    return lim_huffman_build(&state->distance_tree, state->distance_lengths, LIM_DIST_SYMBOLS);
}

bool xx_lim_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written) {
    lim_bits bits;
    lim_state *state;
    uint64_t produced = 0U;
    int32_t position = 0;
    bool done = false;
    bool result;

    if (written) *written = 0U;
    if ((!input && (input_size != 0U)) || (!output && (output_size != 0U))) return false;
    if (output_size > 0x7fffffffU) return false;

    /* The reference truncates its result to the declared length and compares,
     * so a zero-length member is trivially satisfied before any bit is read. */
    if (output_size == 0U) return true;

    state = (lim_state *)xx_mem_alloc(sizeof(lim_state));
    if (!state) return false;

    xx_rt_memset(state, 0, sizeof(lim_state));

    bits.data = input;
    bits.size = input_size;
    bits.position = 0U;
    bits.accumulator = 0U;
    bits.count = 0;

    while (!done) {
        const int32_t block = lim_get(&bits, 14);
        int32_t left;

        if (block <= 0) break; /* 0 ends the stream, -1 means exhausted */

        if (!lim_read_tables(&bits, state)) break;

        for (left = block; left > 0; --left) {
            const int32_t symbol = lim_huffman_decode(&state->main_tree, &bits);
            int32_t k;
            int32_t distance;
            int32_t base_length;
            int32_t source;
            int32_t wide;
            int32_t length;
            int32_t p;
            int32_t q;
            int32_t v;
            int32_t i;

            if (symbol < 0) {
                done = true;
                break;
            }

            if (symbol < 0x100) {
                state->window[position] = (uint8_t)symbol;
                state->history[position] = 0;
                position = (position + 1) & LIM_WINDOW_MASK;
                if (produced < (uint64_t)output_size) output[produced] = (uint8_t)symbol;
                ++produced;
                continue;
            }

            k = symbol - 0x100;
            if (k >= 41) {
                done = true;
                break;
            }

            distance = 0;
            base_length = 0;

            if (k < LIM_SHORT_CODES) {
                distance = (int32_t)g_lim_code[k];
                if ((symbol & 0xff) > 1) {
                    const int32_t extra = lim_get(&bits, k - 1);
                    if (extra < 0) {
                        done = true;
                        break;
                    }
                    distance += extra;
                }
                base_length = 3;
            } else {
                int32_t value = (int32_t)g_lim_code[k];
                int32_t distance_symbol;

                if (value > 0xff) {
                    const int32_t extra = lim_get(&bits, value >> 8);
                    if (extra < 0) {
                        done = true;
                        break;
                    }
                    value = (value & 0xff00) | (((value & 0xff) + extra) & 0xff);
                }

                base_length = value & 0xff;

                distance_symbol = lim_huffman_decode(&state->distance_tree, &bits);
                if ((distance_symbol < 0) || (distance_symbol >= LIM_DIST_SYMBOLS)) {
                    done = true;
                    break;
                }

                distance = (int32_t)g_lim_distance_base[distance_symbol];
                if (g_lim_distance_extra[distance_symbol]) {
                    const int32_t extra = lim_get(&bits, (int32_t)g_lim_distance_extra[distance_symbol]);
                    if (extra < 0) {
                        done = true;
                        break;
                    }
                    distance += extra;
                }
            }

            source = (int32_t)((~((uint32_t)distance - (uint32_t)position)) & (uint32_t)LIM_WINDOW_MASK);

            wide = base_length + (int32_t)state->history[source];
            length = wide & 0xff;
            if (wide > 0xff) length = (length + 4) & 0xff;

            p = position;
            for (i = 0; i < length; ++i) {
                state->history[p] = 0;
                p = (p + 1) & LIM_WINDOW_MASK;
            }

            q = source;
            for (v = (length - 3) & 0xff; v != 0; --v) {
                state->history[q] = (uint8_t)v;
                q = (q + 1) & LIM_WINDOW_MASK;
            }

            for (i = 0; i < length; ++i) {
                const uint8_t byte = state->window[source];
                state->window[position] = byte;
                if (produced < (uint64_t)output_size) output[produced] = byte;
                ++produced;
                position = (position + 1) & LIM_WINDOW_MASK;
                source = (source + 1) & LIM_WINDOW_MASK;
            }

            if (produced >= (uint64_t)output_size) {
                done = true;
                break;
            }
        }
    }

    xx_mem_free(state);

    /* The reference truncates an overshoot and then requires an exact match,
     * so more than the declared length is fine and less is a failure. */
    result = (produced >= (uint64_t)output_size);

    if (result && written) *written = output_size;

    return result;
}
