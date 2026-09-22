/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Yoshizaki LZHUF (LZSS over an adaptive Huffman tree) as embedded by the BWCF
 * and ZTC containers, plus the LZW codec of the XLZHCXP container, which is
 * named after the family but is not one of it.
 *
 * Ported from the reference decoders Algos/xlzhufdecoder.cpp (the parameterised
 * LZHUF core), Algos/xztcdecoder.cpp (the ZTC page framing) and
 * Algos/xlzhcxpdecoder.cpp (the XLZHCXP LZW). The tree, the bit reader, the
 * ring rules and every bias below are the reference's, not a reconstruction.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/lzhuf/xx_lzhuf.h"

/* ------------------------------------------------------------------------- */
/* Shared position tables                                                     */
/* ------------------------------------------------------------------------- */

/* LHA's classic "-lh1-" position tables: the six-bit distance prefix and the
 * number of bits that prefix was coded in. Both LZHUF and LHA build these at
 * run time from the value ranges {0} {1..3} {4..11} {12..23} {24..47} {48..63}
 * coded in 3,4,5,6,7,8 bits; spelling them out avoids a run-time initialiser
 * and keeps the module free of mutable module-level state. */
static const uint8_t xx_lzhuf_d_code[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0c, 0x0c, 0x0c, 0x0c, 0x0d, 0x0d, 0x0d, 0x0d,
    0x0e, 0x0e, 0x0e, 0x0e, 0x0f, 0x0f, 0x0f, 0x0f,
    0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
    0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
    0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
    0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1a, 0x1a, 0x1b, 0x1b,
    0x1c, 0x1c, 0x1d, 0x1d, 0x1e, 0x1e, 0x1f, 0x1f,
    0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
    0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
    0x28, 0x28, 0x29, 0x29, 0x2a, 0x2a, 0x2b, 0x2b,
    0x2c, 0x2c, 0x2d, 0x2d, 0x2e, 0x2e, 0x2f, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
};

static const uint8_t xx_lzhuf_d_len[16] = {
    3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8
};

/* ------------------------------------------------------------------------- */
/* Byte source: flat, or ZTC's paged payload                                  */
/* ------------------------------------------------------------------------- */

#define XX_ZTC_PAGE_SIZE 0x1000U
#define XX_ZTC_PAGE_CHECK_SIZE 4U

typedef struct xx_lzhuf_source {
    const uint8_t *data;
    size_t size;
    size_t position;  /* next byte to serve */
    size_t page_end;  /* end of the current page; == size when not paged */
    size_t remaining; /* ZTC page budget, unused when not paged */
    bool paged;
    bool ended;
} xx_lzhuf_source;

static void xx_lzhuf_source_init(xx_lzhuf_source *source, const uint8_t *data,
                                 size_t size, bool paged) {
    source->data = data;
    source->size = size;
    source->position = 0U;
    source->page_end = paged ? 0U : size;
    source->remaining = size;
    source->paged = paged;
    source->ended = false;
}

/* Step to the next ZTC page. The check field is charged to the budget BEFORE
 * the page length is chosen, which is what makes the last page short by exactly
 * four; that is deliberate and must not be "fixed". A zero-length page is legal
 * and simply costs four bytes, so this loops rather than failing on one. The
 * sums are not re-checked here: xx_lzhuf_ztc_verify_pages() has already walked
 * the whole payload, exactly as the reference's depage step does. */
static bool xx_lzhuf_source_next_page(xx_lzhuf_source *source) {
    for (;;) {
        size_t page;
        if (source->remaining < XX_ZTC_PAGE_CHECK_SIZE) return false;
        source->remaining -= XX_ZTC_PAGE_CHECK_SIZE;
        page = (source->remaining >= XX_ZTC_PAGE_SIZE) ? XX_ZTC_PAGE_SIZE
                                                       : source->remaining;
        if (page > source->size - source->position) return false;
        if ((source->size - source->position) - page < XX_ZTC_PAGE_CHECK_SIZE) {
            return false;
        }
        source->remaining -= page;
        if (page != 0U) {
            source->page_end = source->position + page;
            return true;
        }
        source->position += XX_ZTC_PAGE_CHECK_SIZE;
    }
}

static bool xx_lzhuf_source_byte(xx_lzhuf_source *source, uint8_t *value) {
    if (source->ended) return false;
    if (source->position >= source->page_end) {
        if (!source->paged) {
            source->ended = true;
            return false;
        }
        /* Skip the check field of the page just finished, then open the next. */
        if (source->page_end != 0U) {
            source->position = source->page_end + XX_ZTC_PAGE_CHECK_SIZE;
        }
        if (!xx_lzhuf_source_next_page(source)) {
            source->ended = true;
            return false;
        }
    }
    *value = source->data[source->position];
    ++source->position;
    return true;
}

/* The reference verifies EVERY page before the codec sees anything and rejects
 * the member when any sum disagrees - including pages past the point the codec
 * stops reading - so this pre-pass has to walk the whole payload too. Its break
 * conditions are not errors: a payload that runs out mid-page simply ends. */
static bool xx_lzhuf_ztc_verify_pages(const uint8_t *input, size_t input_size) {
    size_t remaining = input_size;
    size_t position = 0U;

    while (remaining >= XX_ZTC_PAGE_CHECK_SIZE) {
        size_t page;
        size_t i;
        uint32_t sum = 0U;
        uint32_t stored;

        remaining -= XX_ZTC_PAGE_CHECK_SIZE;
        page = (remaining >= XX_ZTC_PAGE_SIZE) ? XX_ZTC_PAGE_SIZE : remaining;
        if (page > input_size - position) break;
        if ((input_size - position) - page < XX_ZTC_PAGE_CHECK_SIZE) break;

        for (i = 0U; i < page; ++i) sum += (uint32_t)input[position + i];

        stored = (uint32_t)input[position + page] |
                 ((uint32_t)input[position + page + 1U] << 8U) |
                 ((uint32_t)input[position + page + 2U] << 16U) |
                 ((uint32_t)input[position + page + 3U] << 24U);
        if (stored != sum) return false;

        position += page + XX_ZTC_PAGE_CHECK_SIZE;
        remaining -= page;
    }

    return true;
}

/* ------------------------------------------------------------------------- */
/* The adaptive Huffman tree and its MSB-first bit reader                     */
/* ------------------------------------------------------------------------- */

/* Sized for the widest variant of the family (N_CHAR 317, an 8 KiB ring) so one
 * state serves every parameter set below. */
#define XX_LZHUF_MAX_NCHAR 317
#define XX_LZHUF_MAX_T (XX_LZHUF_MAX_NCHAR * 2 - 1)
#define XX_LZHUF_MAX_RING 8192

typedef struct xx_lzhuf_options {
    /* 0 -> position = d_code[i]<<5 | (x & 0x1F), extra = d_len[i>>4]-3 (2 KiB)
     * 1 -> position = d_code[i]<<6 | (x & 0x3F), extra = d_len[i>>4]-2 (4 KiB)
     * 2 -> position = d_code[i]<<7 | (x & 0x7F), extra = d_len[i>>4]-1 (8 KiB) */
    int dist_variant;
    int n_char;         /* alphabet size; T = 2*n_char-1, R = T-1 */
    int ring_size;      /* power of two; positions are masked with ring_size-1 */
    int ring_fill;      /* byte the ring starts filled with */
    int max_freq;       /* 0x8000 or 0xD000 */
    int eof_code;       /* symbol that ends the stream, -1 for none */
    bool shift_above_eof;
    int length_bias;    /* length = symbol - 0xFF + length_bias */
    int match_bias;     /* source = (cursor - distance - match_bias) & mask */
    bool reconstruct;   /* at max_freq: halve and rebuild, else stop updating */
} xx_lzhuf_options;

typedef struct xx_lzhuf_state {
    xx_lzhuf_source source;
    uint32_t buffer;
    int count;
    bool eof;
    int n_char;
    int t;
    int r;
    int max_freq;
    int dist_variant;
    bool reconstruct;
    uint16_t freq[XX_LZHUF_MAX_T + 1];
    int32_t son[XX_LZHUF_MAX_T];
    int32_t prnt[XX_LZHUF_MAX_T + XX_LZHUF_MAX_NCHAR];
    uint8_t ring[XX_LZHUF_MAX_RING];
} xx_lzhuf_state;

/* Byte pump feeding the 16-bit window that lives in bits 15..0 of a 32-bit
 * register. The shift below is the reference's own expression; count is only
 * ever 0..7 when this is called, so the & 0x1F never bites. */
static void xx_lzhuf_fill(xx_lzhuf_state *state) {
    uint32_t byte = 0U;
    uint8_t value = 0U;

    if (!state->eof && xx_lzhuf_source_byte(&state->source, &value)) {
        byte = (uint32_t)value;
    } else {
        state->eof = true;
    }

    state->buffer |= byte << ((unsigned)(8 - state->count) & 0x1FU);
    state->count += 8;
}

static int xx_lzhuf_get_bit(xx_lzhuf_state *state) {
    uint32_t value;

    while (state->count == 0) {
        xx_lzhuf_fill(state);
        if (state->eof) break;
    }

    value = state->buffer;
    state->buffer <<= 1;
    --state->count;

    return (value & 0x8000U) ? 1 : 0;
}

static int xx_lzhuf_get_byte(xx_lzhuf_state *state) {
    uint32_t value;

    while (state->count < 8) {
        xx_lzhuf_fill(state);
        if (state->eof) break;
    }

    value = state->buffer;
    state->buffer <<= 8;
    state->count -= 8;

    return (int)((value >> 8U) & 0xFFU);
}

static void xx_lzhuf_start_tree(xx_lzhuf_state *state) {
    int i;
    int j;

    for (i = 0; i < state->n_char; ++i) {
        state->freq[i] = (uint16_t)1U;
        state->son[i] = i + state->t;
        state->prnt[i + state->t] = i;
    }

    i = 0;
    j = state->n_char;
    while (j <= state->r) {
        state->freq[j] = (uint16_t)(state->freq[i] + state->freq[i + 1]);
        state->son[j] = i;
        state->prnt[i] = j;
        state->prnt[i + 1] = j;
        i += 2;
        ++j;
    }

    /* freq[T] is the sentinel the re-weighting walk stops on. */
    state->freq[state->t] = 0xFFFFU;
    state->prnt[state->r] = 0;
}

static void xx_lzhuf_reconstruct(xx_lzhuf_state *state) {
    int i;
    int j;
    int n;

    j = 0;
    for (i = 0; i < state->t; ++i) {
        if (state->son[i] >= state->t) {
            state->freq[j] = (uint16_t)((state->freq[i] + 1) >> 1);
            state->son[j] = state->son[i];
            ++j;
        }
    }

    i = 0;
    j = state->n_char;
    while (j < state->t) {
        int k;
        uint16_t freq = (uint16_t)(state->freq[i] + state->freq[i + 1]);
        state->freq[j] = freq;

        k = j - 1;
        /* The frequencies below j are already sorted, so this walk always
         * stops; the k >= 0 bound only matters for a tree corrupted by a bad
         * stream, where the original would index off the front of the array. */
        while ((k >= 0) && (freq < state->freq[k])) --k;
        ++k;

        for (n = j; n > k; --n) {
            state->freq[n] = state->freq[n - 1];
            state->son[n] = state->son[n - 1];
        }

        state->freq[k] = freq;
        state->son[k] = i;

        i += 2;
        ++j;
    }

    for (n = 0; n < state->t; ++n) {
        int k = state->son[n];
        if (k < state->t) {
            state->prnt[k] = n;
            state->prnt[k + 1] = n;
        } else {
            state->prnt[k] = n;
        }
    }
}

static void xx_lzhuf_update(xx_lzhuf_state *state, int symbol) {
    int c;

    if ((int)state->freq[state->r] == state->max_freq) {
        /* Some embedders simply stop re-weighting from here instead of halving
         * and rebuilding. Neither is reached by the current corpora, but they
         * are not interchangeable. */
        if (!state->reconstruct) return;
        xx_lzhuf_reconstruct(state);
    }

    c = state->prnt[symbol + state->t];

    do {
        uint16_t freq;
        int l;

        state->freq[c] = (uint16_t)(state->freq[c] + 1);
        freq = state->freq[c];
        l = c + 1;

        if (state->freq[l] < freq) {
            /* freq[T] is the 0xFFFF sentinel that stops this walk, so l may
             * legitimately reach T - one past the last node - before the
             * decrement below pulls it back into range. */
            int i;
            int j;

            l = c + 2;
            while ((l <= state->t) && (state->freq[l] < freq)) ++l;
            --l;
            if ((l < 0) || (l >= state->t)) return;

            state->freq[c] = state->freq[l];
            state->freq[l] = freq;

            i = state->son[c];
            state->prnt[i] = l;
            if (i < state->t) state->prnt[i + 1] = l;

            j = state->son[l];
            state->son[l] = i;
            state->prnt[j] = c;
            if (j < state->t) state->prnt[j + 1] = c;

            state->son[c] = j;
            c = l;
        }

        c = state->prnt[c];
    } while (c != 0);
}

/* -1 on a short stream or a corrupt tree. */
static int xx_lzhuf_decode_char(xx_lzhuf_state *state) {
    int code = state->son[state->r];

    while (code < state->t) {
        int bit = xx_lzhuf_get_bit(state);
        int index;
        if (state->eof) return -1;
        index = code + bit;
        if ((index < 0) || (index >= state->t)) return -1;
        code = state->son[index];
    }

    code -= state->t;
    if ((code < 0) || (code >= state->n_char)) return -1;
    xx_lzhuf_update(state, code);

    return code;
}

/* -1 on a short stream. */
static int xx_lzhuf_decode_position(xx_lzhuf_state *state) {
    int byte = xx_lzhuf_get_byte(state);
    int bits;
    int base;
    int extra;
    int mask;

    if (state->eof) return -1;
    byte &= 0xFF;

    bits = (int)xx_lzhuf_d_len[byte >> 4];

    if (state->dist_variant == 0) {
        base = ((int)xx_lzhuf_d_code[byte]) << 5;
        extra = bits - 3;
        mask = 0x1F;
    } else if (state->dist_variant == 1) {
        base = ((int)xx_lzhuf_d_code[byte]) << 6;
        extra = bits - 2;
        mask = 0x3F;
    } else {
        base = ((int)xx_lzhuf_d_code[byte]) << 7;
        extra = bits - 1;
        mask = 0x7F;
    }

    while (extra > 0) {
        --extra;
        byte = ((byte << 1) + xx_lzhuf_get_bit(state)) & 0xFFFF;
        if (state->eof) return -1;
    }

    return base | (byte & mask);
}

/* ------------------------------------------------------------------------- */
/* The LZHUF pipeline                                                         */
/* ------------------------------------------------------------------------- */

static bool xx_lzhuf_run(const uint8_t *input, size_t input_size,
                         const xx_lzhuf_options *options, bool paged,
                         uint8_t *output, size_t output_size, size_t *written) {
    xx_lzhuf_state *state;
    uint32_t mask;
    uint32_t cursor = 0U;
    size_t produced = 0U;
    bool has_eof;
    bool finished;
    bool ok = false;

    if (written) *written = 0U;
    if (!input && (input_size != 0U)) return false;
    if (!output && (output_size != 0U)) return false;
    if ((options->dist_variant < 0) || (options->dist_variant > 2)) return false;
    if ((options->n_char < 0x101) || (options->n_char > XX_LZHUF_MAX_NCHAR)) {
        return false;
    }
    if ((options->ring_size < 0x100) || (options->ring_size > XX_LZHUF_MAX_RING)) {
        return false;
    }
    if ((options->ring_size & (options->ring_size - 1)) != 0) return false;
    if ((options->length_bias < 0) || (options->length_bias > 0x100)) return false;
    if ((options->match_bias < 0) || (options->match_bias > 1)) return false;
    if (options->eof_code >= options->n_char) return false;

    has_eof = (options->eof_code >= 0);
    mask = (uint32_t)options->ring_size - 1U;
    finished = !has_eof;

    /* ~16 KiB of tree, parent map and ring: heap rather than stack so a caller
     * on a small thread stack is safe. Nothing here is shared or static. */
    state = (xx_lzhuf_state *)xx_mem_alloc(sizeof(xx_lzhuf_state));
    if (!state) return false;
    xx_rt_memset(state, 0, sizeof(*state));

    xx_lzhuf_source_init(&state->source, input, input_size, paged);
    state->n_char = options->n_char;
    state->t = options->n_char * 2 - 1;
    state->r = options->n_char * 2 - 2;
    state->max_freq = options->max_freq;
    state->dist_variant = options->dist_variant;
    state->reconstruct = options->reconstruct;
    xx_rt_memset(state->ring, options->ring_fill, (size_t)options->ring_size);
    xx_lzhuf_start_tree(state);

    while (has_eof || (produced < output_size)) {
        int code = xx_lzhuf_decode_char(state);
        int distance;
        uint32_t source;
        size_t length;

        if (code < 0) break;

        if (code < 0x100) {
            if (produced >= output_size) goto done;
            output[produced++] = (uint8_t)code;
            state->ring[cursor] = (uint8_t)code;
            cursor = (cursor + 1U) & mask;
            continue;
        }

        if (has_eof) {
            if (code == options->eof_code) {
                finished = true;
                break;
            }
            if (options->shift_above_eof && (code > options->eof_code)) --code;
        }

        distance = xx_lzhuf_decode_position(state);
        if (distance < 0) break;

        /* Unsigned so the wrap is defined; the mask makes every reference land
         * inside the ring. A reference to a slot the stream has not written yet
         * reads the preset fill byte, which is LZSS's own rule and what the
         * encoder assumed - it is not an out-of-bounds read. */
        source = (cursor - (uint32_t)distance - (uint32_t)options->match_bias) &
                 mask;

        if ((code - 0xFF + options->length_bias) <= 0) goto done;
        length = (size_t)(code - 0xFF + options->length_bias);

        /* Without an end symbol the stored size is the only stop condition, so
         * a final match that overshoots it is truncated rather than rejected.
         * That is the reference's behaviour and the format's own rule; it is
         * deliberate and must not be "fixed" into an error. */
        if (!has_eof) {
            if (length > (output_size - produced)) length = output_size - produced;
        }

        while (length > 0U) {
            uint8_t byte;
            if (produced >= output_size) goto done;
            byte = state->ring[source];
            output[produced++] = byte;
            state->ring[cursor] = byte;
            cursor = (cursor + 1U) & mask;
            source = (source + 1U) & mask;
            --length;
        }
    }

    if (!finished) goto done;

    ok = (produced == output_size);

done:
    if (ok && written) *written = produced;
    xx_mem_free(state);

    return ok;
}

/* The parameter set BWCF, ZTC, SBX and ARNI all carry. */
static void xx_lzhuf_plain_options(xx_lzhuf_options *options) {
    /* F = 0x3C, THRESHOLD = 2 -> N_CHAR = 0x100 - (2 - 60) = 314, T = 627,
     * R = 626. The 0x2000-byte ring is deliberately wider than the encoder's
     * 4 KiB window, which is harmless: distances never reach past 4 KiB, so the
     * extra half is only ever read as preset fill. */
    options->dist_variant = 1;
    options->n_char = 314;
    options->ring_size = 0x2000;
    options->ring_fill = 0x20;
    options->max_freq = 0x8000;
    options->eof_code = -1;
    options->shift_above_eof = false;
    options->length_bias = 2;
    options->match_bias = 1;
    options->reconstruct = true;
}

bool xx_lzhuf_decode_memory(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size,
                            size_t *written) {
    xx_lzhuf_options options;

    if (written) *written = 0U;
    xx_lzhuf_plain_options(&options);

    return xx_lzhuf_run(input, input_size, &options, false, output, output_size,
                        written);
}

bool xx_lzhuf_ztc_decode_memory(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size,
                                size_t *written) {
    xx_lzhuf_options options;

    if (written) *written = 0U;
    if (!input && (input_size != 0U)) return false;
    if (!xx_lzhuf_ztc_verify_pages(input, input_size)) return false;
    xx_lzhuf_plain_options(&options);

    return xx_lzhuf_run(input, input_size, &options, true, output, output_size,
                        written);
}

/* ------------------------------------------------------------------------- */
/* XLZHCXP - block-framed LZW, a different lineage entirely                    */
/* ------------------------------------------------------------------------- */

#define XX_LZHCXP_CODE_CLEAR 0x200
#define XX_LZHCXP_CODE_END 0x201
#define XX_LZHCXP_FIRST_FREE 0x202
#define XX_LZHCXP_MAX_CODE 0x1000
#define XX_LZHCXP_MIN_WIDTH 10
#define XX_LZHCXP_MAX_WIDTH 12

typedef struct xx_lzhcxp_state {
    const uint8_t *data;
    size_t size;
    size_t position;
    int block_left;
    uint32_t accumulator;
    int bit_count;
    uint16_t prefix[XX_LZHCXP_MAX_CODE];
    uint8_t suffix[XX_LZHCXP_MAX_CODE];
    uint8_t stack[XX_LZHCXP_MAX_CODE];
} xx_lzhcxp_state;

/* [u8 length][length bytes] ... ; a zero-length block is the end marker, and
 * its length byte counts as consumed. */
static bool xx_lzhcxp_read_byte(xx_lzhcxp_state *state, int *byte) {
    if (state->block_left == 0) {
        if (state->position >= state->size) return false;
        state->block_left = (int)state->data[state->position++];
        if (state->block_left < 1) return false;
    }
    if (state->position >= state->size) return false;
    *byte = (int)state->data[state->position++];
    --state->block_left;
    return true;
}

static bool xx_lzhcxp_read_bits(xx_lzhcxp_state *state, int width, int *value) {
    while (state->bit_count < width) {
        int byte = 0;
        if (!xx_lzhcxp_read_byte(state, &byte)) return false;
        state->accumulator |= (uint32_t)byte << state->bit_count;
        state->bit_count += 8;
    }
    *value = (int)(state->accumulator & ((1U << width) - 1U));
    state->accumulator >>= width;
    state->bit_count -= width;
    return true;
}

/* With output non-NULL the bytes go there and limit is the exact expected size;
 * with output NULL nothing is kept, so a container that stores no uncompressed
 * size can still be measured. LZW rebuilds every match out of the dictionary
 * rather than out of past output, so discarding needs no window and the two
 * paths cannot disagree. */
static bool xx_lzhcxp_run(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t limit, size_t *produced,
                          size_t *consumed) {
    xx_lzhcxp_state *state;
    size_t output_at = 0U;
    int width = XX_LZHCXP_MIN_WIDTH;
    int next_free = XX_LZHCXP_FIRST_FREE;
    int code_limit = 1 << XX_LZHCXP_MIN_WIDTH;
    int previous = 0;
    int first_character = 0;
    int i;
    bool ok = false;

    if (produced) *produced = 0U;
    if (consumed) *consumed = 0U;
    if (!input || (input_size == 0U) || (limit == 0U)) return false;

    state = (xx_lzhcxp_state *)xx_mem_alloc(sizeof(xx_lzhcxp_state));
    if (!state) return false;
    xx_rt_memset(state, 0, sizeof(*state));
    state->data = input;
    state->size = input_size;
    for (i = 0; i < 256; ++i) state->suffix[i] = (uint8_t)i;

    for (;;) {
        int code = 0;
        int original_code;
        int stack_top = 0;

        /* The widening test happens before the read, on the CURRENT next-free
         * code; there is no "early change" fudge here. */
        if ((code_limit <= next_free) && (width < XX_LZHCXP_MAX_WIDTH)) {
            ++width;
            code_limit = 1 << width;
        }

        if (!xx_lzhcxp_read_bits(state, width, &code)) {
            /* Exhausted input is the ordinary way these streams stop. */
            break;
        }
        original_code = code;

        if (code == XX_LZHCXP_CODE_CLEAR) {
            int seed = 0;
            width = XX_LZHCXP_MIN_WIDTH;
            next_free = XX_LZHCXP_FIRST_FREE;
            code_limit = 1 << XX_LZHCXP_MIN_WIDTH;
            if (!xx_lzhcxp_read_bits(state, width, &seed)) goto done;
            /* The seed keeps its full width as the previous code but only its
             * low eight bits are emitted. Deliberate, and load-bearing. */
            previous = seed;
            first_character = seed;
            if (output_at >= limit) goto done;
            if (output) output[output_at] = (uint8_t)(seed & 0xFF);
            ++output_at;
            continue;
        }
        if (code == XX_LZHCXP_CODE_END) break;
        if ((code > 0xFF) && (code < XX_LZHCXP_CODE_CLEAR)) goto done;

        if (code >= next_free) {
            /* KwKwK: the code that is not in the table yet. */
            state->stack[stack_top++] = (uint8_t)(first_character & 0xFF);
            code = previous;
        }
        while (code > 0xFF) {
            if (stack_top >= XX_LZHCXP_MAX_CODE) goto done;
            if (code >= XX_LZHCXP_MAX_CODE) goto done;
            state->stack[stack_top++] = state->suffix[code];
            code = (int)state->prefix[code];
        }
        first_character = code;
        if (stack_top >= XX_LZHCXP_MAX_CODE) goto done;
        state->stack[stack_top++] = (uint8_t)(code & 0xFF);

        if ((size_t)stack_top > (limit - output_at)) goto done;
        while (stack_top > 0) {
            --stack_top;
            if (output) output[output_at] = state->stack[stack_top];
            ++output_at;
        }

        if (next_free < XX_LZHCXP_MAX_CODE) {
            state->prefix[next_free] = (uint16_t)previous;
            state->suffix[next_free] = (uint8_t)(code & 0xFF);
            ++next_free;
        }
        previous = original_code;
    }

    /* Both ways out of the loop above - the END code and exhausted input - are
     * a successful end of stream in the reference. */
    ok = true;

    if (produced) *produced = output_at;
    if (consumed) *consumed = state->position;

done:
    xx_mem_free(state);

    return ok;
}

bool xx_lzhuf_lzhcxp_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written) {
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!output || (output_size == 0U)) return false;
    if (!xx_lzhcxp_run(input, input_size, output, output_size, &produced, NULL)) {
        return false;
    }
    if (produced != output_size) return false;
    if (written) *written = produced;

    return true;
}

bool xx_lzhuf_lzhcxp_scan_memory(const uint8_t *input, size_t input_size,
                                 size_t max_output, size_t *consumed,
                                 size_t *produced) {
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (max_output == 0U) return false;

    return xx_lzhcxp_run(input, input_size, NULL, max_output, produced, consumed);
}
