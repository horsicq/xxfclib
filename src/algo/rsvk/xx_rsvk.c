/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RSVKDATA / DLIBDATA member codec.  Ported one-for-one from the XArchive
 * reference decoder (XArchive/Algos/xrsvkdecoder.cpp): CACM-1987 adaptive
 * arithmetic decoding over a structured selector/group model, the RUNA/RUNB
 * zero-run code, inverse move-to-front and an inverse Burrows-Wheeler with an
 * explicit sentinel row.  Every constant, every loop bound and the exact
 * renormalisation order are reproduced.
 *
 * Structural differences, all output-equivalent:
 *   - the reference's std::vector scratch and the per-block QByteArray live in
 *     one caller-owned heap block here, and the inverse-BWT walk writes
 *     straight into the caller's output buffer instead of into a temporary
 *     that is then appended;
 *   - the CRC-32 table is built per call into that block instead of into a
 *     function-local static (this library forbids mutable global state);
 *   - the scratch buffers are sized to min(RSVK_MAX_ROWS - 1, output_size)
 *     rather than to the reference's RSVK_MAX_MTF.  A block longer than that
 *     is rejected by the reference too -- either by its RSVK_MAX_ROWS row cap
 *     or by its "block overruns the declared member size" check -- so no
 *     stream that the reference accepts is rejected here.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/rsvk/xx_rsvk.h"

#define RSVK_BLOCK_HEADER_SIZE 20
/* The reference implementation's own ceilings. */
#define RSVK_MAX_MTF 0xc87d2
#define RSVK_MAX_ROWS 0x643e8
#define RSVK_SYMBOL_EOB 0x101
#define RSVK_GROUPS 9
#define RSVK_SELECTOR_MAX_FREQUENCY 0x1000
#define RSVK_SELECTOR_INCREMENT 0x20
#define RSVK_MAX_SYMBOLS 132 /* group 8 is the widest: 130 chars + 2 */

static const int32_t g_group_low[RSVK_GROUPS] = {0, 1, 2, 4, 8, 16, 32, 64, 128};
static const int32_t g_group_high[RSVK_GROUPS] = {0, 1, 3, 7,  15,
                                                  31, 63, 127, 257};
/* Entries 0 and 1 are unused: those two groups are the bare RUNA/RUNB symbols
 * and get no sub-model.  Groups 7 and 8 never reach their rescale threshold in
 * the reference corpus, so their values are the smallest powers of two
 * consistent with every known block -- do not "tidy" them. */
static const int32_t g_group_max_frequency[RSVK_GROUPS] = {
    0, 0, 0x100, 0x100, 0x80, 0x400, 0x800, 0x1000, 0x2000};

/* MSB-first bit reader; reading past the end yields zero bits, which is what
 * the reference does when the bounded sub-stream is exhausted. */
typedef struct rsvk_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t current;
    int32_t count;
} rsvk_bits;

/* One adaptive frequency model: the CACM 1987 "start_model"/"update_model"
 * pair with a configurable increment and rescale threshold. */
typedef struct rsvk_model_s {
    int32_t base;
    int32_t chars;   /* number of real characters */
    int32_t symbols; /* CACM No_of_symbols == chars + 1 (one phantom) */
    int32_t max_frequency;
    int32_t increment;
    int32_t char_to_index[RSVK_MAX_SYMBOLS + 2];
    int32_t index_to_char[RSVK_MAX_SYMBOLS + 2];
    int32_t frequency[RSVK_MAX_SYMBOLS + 2];
    int32_t cumulative[RSVK_MAX_SYMBOLS + 2];
} rsvk_model;

typedef struct rsvk_arith_s {
    rsvk_bits *bits;
    int32_t low;
    int32_t high;
    int32_t value;
} rsvk_arith;

typedef struct rsvk_state_s {
    rsvk_model selector;
    rsvk_model groups[RSVK_GROUPS];
    uint32_t crc_table[256];
    uint8_t *mtf;    /* capacity bytes            */
    uint8_t *column; /* capacity + 1 bytes        */
    int32_t *succ;   /* capacity + 1 entries      */
    int32_t counts[257];
    int32_t base[257];
    size_t capacity;
    size_t mtf_size;
} rsvk_state;

static void rsvk_crc_init(uint32_t *table) {
    uint32_t i;
    for (i = 0; i < 256U; ++i) {
        uint32_t c = i;
        int32_t k;
        for (k = 0; k < 8; ++k) {
            c = (c & 1U) ? (0xedb88320U ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
}

static uint32_t rsvk_crc32(const uint32_t *table, const uint8_t *data,
                           size_t size) {
    uint32_t crc = 0xffffffffU;
    size_t i;
    for (i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xffU] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffU;
}

static int32_t rsvk_read_bit(rsvk_bits *bits) {
    int32_t bit;
    if (bits->count == 0) {
        bits->current =
            (bits->position < bits->size) ? bits->data[bits->position] : 0U;
        ++bits->position;
        bits->count = 8;
    }
    bit = (bits->current >> 7) & 1;
    bits->current = (uint8_t)(bits->current << 1);
    --bits->count;
    return bit;
}

static bool rsvk_model_init(rsvk_model *model, int32_t low, int32_t high,
                            int32_t max_frequency, int32_t increment) {
    int32_t i;
    xx_rt_memset(model, 0, sizeof(*model));
    model->base = low;
    model->chars = high - low + 1;
    model->symbols = model->chars + 1;
    if ((model->chars <= 0) || (model->symbols > RSVK_MAX_SYMBOLS)) return false;
    model->max_frequency = max_frequency;
    model->increment = increment;
    for (i = 0; i < model->chars; ++i) {
        model->char_to_index[i] = i + 1;
        model->index_to_char[i + 1] = i;
    }
    for (i = 0; i <= model->symbols; ++i) {
        model->frequency[i] = 1;
        model->cumulative[i] = model->symbols - i;
    }
    model->frequency[0] = 0;
    return true;
}

static void rsvk_model_update(rsvk_model *model, int32_t index) {
    int32_t i;
    if (model->max_frequency <= model->cumulative[0]) {
        int32_t running = 0;
        for (i = model->symbols; i >= 0; --i) {
            model->frequency[i] = (model->frequency[i] + 1) / 2;
            model->cumulative[i] = running;
            running += model->frequency[i];
        }
    }
    i = index;
    while ((i > 0) && (model->frequency[i] == model->frequency[i - 1])) --i;
    if (i < index) {
        const int32_t char_at_i = model->index_to_char[i];
        const int32_t char_at_n = model->index_to_char[index];
        model->index_to_char[i] = char_at_n;
        model->index_to_char[index] = char_at_i;
        model->char_to_index[char_at_i] = index;
        model->char_to_index[char_at_n] = i;
    }
    model->frequency[i] += model->increment;
    while (i > 0) {
        --i;
        model->cumulative[i] += model->increment;
    }
}

static void rsvk_arith_init(rsvk_arith *arith, rsvk_bits *bits) {
    int32_t i;
    arith->bits = bits;
    arith->value = 0;
    for (i = 0; i < 16; ++i) {
        arith->value = arith->value * 2 + rsvk_read_bit(bits);
    }
    arith->low = 0;
    arith->high = 0xffff;
}

/* Returns the decoded character (0 .. model->chars - 1), or -1 on a stream
 * that selects the model's phantom symbol. */
static int32_t rsvk_arith_decode(rsvk_arith *arith, rsvk_model *model) {
    const int32_t range = arith->high - arith->low + 1;
    const int32_t total = model->cumulative[0];
    int32_t target;
    int32_t index = 1;
    int32_t character;
    if ((range <= 0) || (total <= 0)) return -1;
    target = (int32_t)((((int64_t)(arith->value - arith->low) + 1) * total - 1) /
                       range);
    while ((index <= model->symbols) && (model->cumulative[index] > target)) {
        ++index;
    }
    if (index > model->chars) return -1;
    character = model->index_to_char[index];
    arith->high =
        arith->low +
        (int32_t)(((int64_t)range * model->cumulative[index - 1]) / total) - 1;
    arith->low =
        arith->low +
        (int32_t)(((int64_t)range * model->cumulative[index]) / total);
    for (;;) {
        if (arith->high >= 0x8000) {
            if (arith->low < 0x8000) {
                if ((arith->low < 0x4000) || (arith->high > 0xbfff)) {
                    /* Deliberate: the model is updated here, at the point the
                     * symbol is finally emitted, not before renormalising. */
                    rsvk_model_update(model, index);
                    return character;
                }
                arith->value -= 0x4000;
                arith->low -= 0x4000;
                arith->high -= 0x4000;
            } else {
                arith->value -= 0x8000;
                arith->low -= 0x8000;
                arith->high -= 0x8000;
            }
        }
        arith->low = arith->low * 2;
        arith->high = arith->high * 2 + 1;
        arith->value = arith->value * 2 + rsvk_read_bit(arith->bits);
    }
}

/* Decodes one symbol: a selector, plus a group offset when the selector is 2
 * or above.  Returns false on a phantom symbol. */
static bool rsvk_next_symbol(rsvk_arith *arith, rsvk_state *state,
                             int32_t *symbol) {
    const int32_t selector = rsvk_arith_decode(arith, &state->selector);
    int32_t offset;
    if (selector < 0) return false;
    if (selector < 2) {
        *symbol = selector;
        return true;
    }
    offset = rsvk_arith_decode(arith, &state->groups[selector]);
    if (offset < 0) return false;
    *symbol = state->groups[selector].base + offset;
    return true;
}

/* Decodes one block's arithmetic stream into the MTF-index sequence. */
static bool rsvk_decode_symbols(rsvk_state *state, const uint8_t *payload,
                                size_t payload_size) {
    rsvk_bits bits;
    rsvk_arith arith;
    int32_t i;
    int32_t symbol = -1;
    bool need_symbol = true;

    bits.data = payload;
    bits.size = payload_size;
    bits.position = 0;
    bits.current = 0U;
    bits.count = 0;
    rsvk_arith_init(&arith, &bits);

    if (!rsvk_model_init(&state->selector, 0, RSVK_GROUPS - 1,
                         RSVK_SELECTOR_MAX_FREQUENCY,
                         RSVK_SELECTOR_INCREMENT)) {
        return false;
    }
    for (i = 0; i < RSVK_GROUPS; ++i) {
        xx_rt_memset(&state->groups[i], 0, sizeof(state->groups[i]));
    }
    for (i = 2; i < RSVK_GROUPS; ++i) {
        if (!rsvk_model_init(&state->groups[i], g_group_low[i], g_group_high[i],
                             g_group_max_frequency[i], 1)) {
            return false;
        }
    }

    state->mtf_size = 0;
    for (;;) {
        if (need_symbol) {
            if (!rsvk_next_symbol(&arith, state, &symbol)) return false;
            need_symbol = false;
        }
        if (symbol == RSVK_SYMBOL_EOB) break;
        if (symbol < 2) {
            /* RUNA/RUNB bijective base-2 run of MTF zeroes. */
            int64_t run = 0;
            int64_t weight = 1;
            for (;;) {
                run += (symbol == 0) ? weight : (weight * 2);
                weight *= 2;
                if ((weight > RSVK_MAX_MTF) || (run > RSVK_MAX_MTF)) {
                    return false;
                }
                if (!rsvk_next_symbol(&arith, state, &symbol)) return false;
                if (symbol >= 2) break;
                /* symbol is the next RUNA/RUNB digit: keep accumulating. */
            }
            if ((int64_t)state->mtf_size + run > (int64_t)state->capacity) {
                return false;
            }
            xx_rt_memset(state->mtf + state->mtf_size, 0, (size_t)run);
            state->mtf_size += (size_t)run;
            /* symbol already holds the symbol that ended the run. */
            continue;
        }
        if (state->mtf_size >= state->capacity) return false;
        state->mtf[state->mtf_size] = (uint8_t)((symbol - 1) & 0xff);
        ++state->mtf_size;
        need_symbol = true;
    }
    return true;
}

/* Inverse MTF + inverse BWT for one block, written straight to `output`. */
static bool rsvk_inverse(rsvk_state *state, int32_t primary, int32_t hole,
                         uint8_t *output) {
    const int32_t length = (int32_t)state->mtf_size;
    const int32_t rows = length + 1;
    uint8_t table[256];
    uint8_t position[256];
    int32_t running = 0;
    int32_t out = 0;
    int32_t i;
    int32_t p;

    if ((hole < 0) || (hole > length)) return false;
    if ((rows <= 0) || (rows > RSVK_MAX_ROWS)) return false;
    if ((primary < 0) || (primary >= rows)) return false;

    xx_rt_memset(state->column, 0, (size_t)rows);
    for (i = 0; i < 256; ++i) {
        table[i] = (uint8_t)i;
        position[i] = (uint8_t)i;
    }
    /* counts[0] is the sentinel, counts[1 + b] the byte b. */
    for (i = 0; i < 257; ++i) state->counts[i] = 0;
    state->counts[0] = 1;

    for (i = 0; i < length; ++i) {
        const uint8_t rank = state->mtf[(size_t)i];
        const uint8_t value = table[rank];
        const uint8_t where = position[value];
        /* The sentinel row is skipped, never written. */
        if (i == hole) ++out;
        if (where != 0) {
            int32_t k;
            for (k = where; k != 0; --k) {
                table[k] = table[k - 1];
                position[table[k]] = (uint8_t)k;
            }
            table[0] = value;
            position[value] = 0;
        }
        if ((out < 0) || (out >= rows)) return false;
        state->column[(size_t)out] = value;
        ++state->counts[1 + value];
        ++out;
    }

    /* Inverse Burrows-Wheeler.  The sentinel sorts before every byte, which is
     * why its running count starts at 1 and its bucket base is 0. */
    for (i = 0; i < 257; ++i) {
        state->base[i] = running;
        running += state->counts[i];
        state->counts[i] = 0;
    }
    for (i = 0; i < hole; ++i) {
        const uint8_t b = state->column[(size_t)i];
        const int32_t index = state->counts[1 + b] + state->base[1 + b];
        if ((index < 0) || (index >= rows)) return false;
        state->succ[(size_t)index] = i;
        ++state->counts[1 + b];
    }
    {
        const int32_t index = state->counts[0] + state->base[0];
        if ((index < 0) || (index >= rows)) return false;
        state->succ[(size_t)index] = hole;
    }
    for (i = hole + 1; i < rows; ++i) {
        const uint8_t b = state->column[(size_t)i];
        const int32_t index = state->counts[1 + b] + state->base[1 + b];
        if ((index < 0) || (index >= rows)) return false;
        state->succ[(size_t)index] = i;
        ++state->counts[1 + b];
    }

    /* The walk produces `rows` characters; the last one is the sentinel row's
     * undefined byte and is dropped. */
    p = primary;
    for (i = 0; i < rows; ++i) {
        if ((p < 0) || (p >= rows)) return false;
        if (i < length) output[i] = state->column[(size_t)p];
        p = state->succ[(size_t)p];
    }
    return true;
}

static uint32_t rsvk_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void rsvk_free(rsvk_state *state) {
    if (!state) return;
    if (state->mtf) xx_mem_free(state->mtf);
    if (state->column) xx_mem_free(state->column);
    if (state->succ) xx_mem_free(state->succ);
    xx_mem_free(state);
}

XXFC_API bool xx_rsvk_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written) {
    static const uint8_t rsvk_magic[4] = {'D', 'A', 'T', 'A'};
    rsvk_state *state;
    size_t capacity;
    size_t cursor = 0;
    size_t produced = 0;
    bool ok = true;

    if (written) *written = 0;
    if (!input && (input_size != 0)) return false;
    if (output_size == 0) {
        /* Matches the reference: an empty member is accepted when the packed
         * extent is empty or at least holds one block header. */
        return (input_size == 0) || (input_size >= RSVK_BLOCK_HEADER_SIZE);
    }
    if (!output) return false;

    capacity = (size_t)(RSVK_MAX_ROWS - 1);
    if (output_size < capacity) capacity = output_size;

    state = (rsvk_state *)xx_mem_alloc(sizeof(rsvk_state));
    if (!state) return false;
    xx_rt_memset(state, 0, sizeof(*state));
    state->capacity = capacity;
    state->mtf = (uint8_t *)xx_mem_alloc(capacity);
    state->column = (uint8_t *)xx_mem_alloc(capacity + 1);
    state->succ = (int32_t *)xx_mem_alloc((capacity + 1) * sizeof(int32_t));
    if (!state->mtf || !state->column || !state->succ) {
        rsvk_free(state);
        return false;
    }
    rsvk_crc_init(state->crc_table);

    while (cursor + RSVK_BLOCK_HEADER_SIZE <= input_size) {
        const uint8_t *header = input + cursor;
        uint32_t expected_crc;
        size_t packed;
        int32_t primary;
        int32_t hole;
        size_t length;

        if (xx_rt_memcmp(header, rsvk_magic, 4) != 0) break;
        expected_crc = rsvk_read_u32(header + 4);
        packed = (size_t)rsvk_read_u32(header + 8);
        primary = (int32_t)rsvk_read_u32(header + 12);
        hole = (int32_t)rsvk_read_u32(header + 16);
        if ((packed == 0) ||
            (packed > input_size - cursor - RSVK_BLOCK_HEADER_SIZE)) {
            ok = false;
            break;
        }
        if (!rsvk_decode_symbols(state, header + RSVK_BLOCK_HEADER_SIZE,
                                 packed)) {
            ok = false;
            break;
        }
        length = state->mtf_size;
        /* The reference checks the CRC first and the member-size overrun
         * second; both are hard failures, so the order is immaterial -- but
         * the space check has to come first here because the block is built
         * directly in the caller's buffer. */
        if (length > output_size - produced) {
            ok = false;
            break;
        }
        if (!rsvk_inverse(state, primary, hole, output + produced)) {
            ok = false;
            break;
        }
        if (rsvk_crc32(state->crc_table, output + produced, length) !=
            expected_crc) {
            ok = false;
            break;
        }
        produced += length;
        cursor += (size_t)RSVK_BLOCK_HEADER_SIZE + packed;
    }

    if (ok && (produced == output_size)) {
        rsvk_free(state);
        if (written) *written = produced;
        return true;
    }
    rsvk_free(state);
    return false;
}
