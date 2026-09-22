/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Clean-room RAR 1.5 (UNP_VER 15) decoder.  The implementation follows the
 * independently documented adaptive-Huffman/LZ state machine in rar-research.
 * It intentionally shares no source with the restricted UnRAR code base.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_rarx_internal.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

#define XX_RARX15_WINDOW_SIZE 0x10000u
#define XX_RARX15_WINDOW_MASK 0xffffu

static const uint16_t g_dec_l1[] = {
    0x8000, 0xa000, 0xc000, 0xd000, 0xe000, 0xea00,
    0xee00, 0xf000, 0xf200, 0xf200, 0xffff
};
static const uint16_t g_pos_l1[] = {
    0, 0, 0, 2, 3, 5, 7, 11, 16, 20, 24, 32, 32
};
static const uint16_t g_dec_l2[] = {
    0xa000, 0xc000, 0xd000, 0xe000, 0xea00,
    0xee00, 0xf000, 0xf200, 0xf240, 0xffff
};
static const uint16_t g_pos_l2[] = {
    0, 0, 0, 0, 5, 7, 9, 13, 18, 22, 26, 34, 36
};
static const uint16_t g_dec_hf0[] = {
    0x8000, 0xc000, 0xe000, 0xf200, 0xf200,
    0xf200, 0xf200, 0xf200, 0xffff
};
static const uint16_t g_pos_hf0[] = {
    0, 0, 0, 0, 0, 8, 16, 24, 33, 33, 33, 33, 33
};
static const uint16_t g_dec_hf1[] = {
    0x2000, 0xc000, 0xe000, 0xf000, 0xf200, 0xf200, 0xf7e0, 0xffff
};
static const uint16_t g_pos_hf1[] = {
    0, 0, 0, 0, 0, 0, 4, 44, 60, 76, 80, 80, 127
};
static const uint16_t g_dec_hf2[] = {
    0x1000, 0x2400, 0x8000, 0xc000, 0xfa00, 0xffff, 0xffff, 0xffff
};
static const uint16_t g_pos_hf2[] = {
    0, 0, 0, 0, 0, 0, 2, 7, 53, 117, 233, 0, 0
};
static const uint16_t g_dec_hf3[] = {
    0x0800, 0x2400, 0xee00, 0xfe80, 0xffff, 0xffff, 0xffff
};
static const uint16_t g_pos_hf3[] = {
    0, 0, 0, 0, 0, 0, 0, 2, 16, 218, 251, 0, 0
};
static const uint16_t g_dec_hf4[] = {
    0xff00, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
};
static const uint16_t g_pos_hf4[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0
};

static const uint8_t g_short_len1[16] = {
    1, 3, 4, 4, 5, 6, 7, 8, 8, 4, 4, 5, 6, 6, 4, 0
};
static const uint8_t g_short_xor1[15] = {
    0x00, 0xa0, 0xd0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe,
    0xff, 0xc0, 0x80, 0x90, 0x98, 0x9c, 0xb0
};
static const uint8_t g_short_len2[16] = {
    2, 3, 3, 3, 4, 4, 5, 6, 6, 4, 4, 5, 6, 6, 4, 0
};
static const uint8_t g_short_xor2[15] = {
    0x00, 0x40, 0x60, 0xa0, 0xd0, 0xe0, 0xf0, 0xf8,
    0xfc, 0xc0, 0x80, 0x90, 0x98, 0x9c, 0xb0
};

typedef struct xx_rarx15_bits_s {
    const uint8_t *data;
    size_t size;
    size_t bit_pos;
    xx_rarx_status_t status;
} xx_rarx15_bits;

struct xx_rarx15_state {
    uint8_t window[XX_RARX15_WINDOW_SIZE];
    size_t window_pos;
    bool first_window_done;

    uint16_t ch_set[256];
    uint16_t ch_set_a[256];
    uint16_t ch_set_b[256];
    uint16_t ch_set_c[256];
    uint8_t n_to_pl[256];
    uint8_t n_to_pl_b[256];
    uint8_t n_to_pl_c[256];

    uint32_t avr_plc;
    uint32_t avr_plc_b;
    uint32_t avr_ln1;
    uint32_t avr_ln2;
    uint32_t avr_ln3;
    uint32_t max_dist3;
    uint32_t nhfb;
    uint32_t nlzb;
    uint32_t num_huf;
    uint32_t buf60;
    bool static_mode;
    uint32_t l_count;
    uint32_t flag_buf;
    int flags_count;
    uint32_t old_distance[4];
    size_t old_distance_pos;
    uint32_t last_distance;
    uint32_t last_length;
};

typedef struct xx_rarx15_ctx_s {
    xx_rarx15_state *state;
    xx_rarx15_bits bits;
    uint8_t *destination;
    size_t destination_size;
    size_t output_pos;
    xx_pd_struct *progress;
    xx_rarx_status_t status;
} xx_rarx15_ctx;

static size_t xx_rarx15_used_bytes(const xx_rarx15_bits *bits) {
    size_t used;
    if (!bits) return 0;
    used = bits->bit_pos / 8u;
    if ((bits->bit_pos & 7u) != 0 && used < SIZE_MAX) ++used;
    return used > bits->size ? bits->size : used;
}

/* Unpack15 historically peeks a 16-bit word.  Missing lookahead bits are
 * zero, but at least one real bit must remain and consumed bits stay bounded. */
static bool xx_rarx15_peek16(xx_rarx15_bits *bits, uint32_t *value) {
    size_t i;
    uint32_t result = 0;
    if (!bits || !value || bits->status != XX_RARX_STATUS_OK ||
        bits->bit_pos / 8u >= bits->size) {
        if (bits) bits->status = XX_RARX_STATUS_TRUNCATED;
        return false;
    }
    for (i = 0; i < 16; ++i) {
        size_t bit_index;
        result <<= 1;
        if (bits->bit_pos > SIZE_MAX - i) {
            bits->status = XX_RARX_STATUS_LIMIT;
            return false;
        }
        bit_index = bits->bit_pos + i;
        if (bit_index / 8u < bits->size) {
            result |= (uint32_t)((bits->data[bit_index / 8u] >>
                                  (7u - (bit_index & 7u))) & 1u);
        }
    }
    *value = result;
    return true;
}

static bool xx_rarx15_consume(xx_rarx15_bits *bits, size_t count) {
    size_t next;
    if (!bits || bits->status != XX_RARX_STATUS_OK ||
        bits->bit_pos > SIZE_MAX - count) {
        if (bits) bits->status = XX_RARX_STATUS_LIMIT;
        return false;
    }
    next = bits->bit_pos + count;
    if (next / 8u > bits->size ||
        (next / 8u == bits->size && (next & 7u) != 0)) {
        bits->status = XX_RARX_STATUS_TRUNCATED;
        return false;
    }
    bits->bit_pos = next;
    return true;
}

static bool xx_rarx15_fail(xx_rarx15_ctx *ctx, xx_rarx_status_t status) {
    if (ctx && ctx->status == XX_RARX_STATUS_OK) ctx->status = status;
    return false;
}

static void xx_rarx15_corr_huff(uint16_t table[256], uint8_t places[256]) {
    size_t pos = 0;
    int rank;
    for (rank = 7; rank >= 0; --rank) {
        size_t i;
        for (i = 0; i < 32; ++i) {
            table[pos] = (uint16_t)((table[pos] & 0xff00u) | (uint16_t)rank);
            ++pos;
        }
    }
    xx_rt_memset(places, 0, 256);
    for (rank = 6; rank >= 0; --rank) {
        places[rank] = (uint8_t)((7 - rank) * 32);
    }
}

static void xx_rarx15_init_huff(xx_rarx15_state *state) {
    size_t i;
    for (i = 0; i < 256; ++i) {
        state->ch_set[i] = (uint16_t)(i << 8);
        state->ch_set_b[i] = (uint16_t)(i << 8);
        state->ch_set_a[i] = (uint16_t)i;
        state->ch_set_c[i] = (uint16_t)((uint16_t)(0u - (uint8_t)i) << 8);
    }
    xx_rt_memset(state->n_to_pl, 0, sizeof(state->n_to_pl));
    xx_rt_memset(state->n_to_pl_b, 0, sizeof(state->n_to_pl_b));
    xx_rt_memset(state->n_to_pl_c, 0, sizeof(state->n_to_pl_c));
    xx_rarx15_corr_huff(state->ch_set_b, state->n_to_pl_b);
}

static void xx_rarx15_reset_non_solid(xx_rarx15_state *state) {
    if (!state) return;
    xx_mem_zero(state, sizeof(*state));
    state->window_pos = 0;
    state->first_window_done = false;
    state->avr_plc_b = 0;
    state->avr_ln1 = 0;
    state->avr_ln2 = 0;
    state->avr_ln3 = 0;
    state->num_huf = 0;
    state->buf60 = 0;
    state->avr_plc = 0x3500;
    state->max_dist3 = 0x2001;
    state->nhfb = 0x80;
    state->nlzb = 0x80;
    state->old_distance[0] = UINT32_MAX;
    state->old_distance[1] = UINT32_MAX;
    state->old_distance[2] = UINT32_MAX;
    state->old_distance[3] = UINT32_MAX;
    state->old_distance_pos = 0;
    state->last_distance = UINT32_MAX;
    state->last_length = 0;
    xx_rarx15_init_huff(state);
}

static bool xx_rarx15_decode_num(xx_rarx15_ctx *ctx, uint32_t number,
                                 uint32_t start_pos,
                                 const uint16_t *decode_table,
                                 size_t decode_count,
                                 const uint16_t *position_table,
                                 size_t position_count,
                                 uint32_t *result) {
    size_t index = 0;
    uint32_t previous = 0;
    uint32_t masked;
    uint32_t shift;
    if (!ctx || !decode_table || !position_table || !result) return false;
    masked = number & 0xfff0u;
    while (index < decode_count && (uint32_t)decode_table[index] <= masked) {
        ++start_pos;
        ++index;
    }
    if (index >= decode_count || start_pos > 16 || start_pos >= position_count) {
        return xx_rarx15_fail(ctx, XX_RARX_STATUS_CORRUPT);
    }
    if (!xx_rarx15_consume(&ctx->bits, (size_t)start_pos)) {
        return xx_rarx15_fail(ctx, ctx->bits.status);
    }
    if (index != 0) previous = decode_table[index - 1];
    shift = 16u - start_pos;
    *result = ((masked - previous) >> shift) + position_table[start_pos];
    return true;
}

#define XX_RARX15_DECODE_NUM(ctx_, value_, start_, dec_, pos_, out_) \
    xx_rarx15_decode_num((ctx_), (value_), (start_), (dec_), \
                         sizeof(dec_) / sizeof((dec_)[0]), (pos_), \
                         sizeof(pos_) / sizeof((pos_)[0]), (out_))

static bool xx_rarx15_put_byte(xx_rarx15_ctx *ctx, uint8_t value) {
    xx_rarx15_state *state;
    if (!ctx || ctx->output_pos >= ctx->destination_size) {
        return xx_rarx15_fail(ctx, XX_RARX_STATUS_CORRUPT);
    }
    if (ctx->progress && (ctx->output_pos & 0xfffu) == 0 &&
        xx_pd_is_stopped(ctx->progress)) {
        return xx_rarx15_fail(ctx, XX_RARX_STATUS_CANCELLED);
    }
    state = ctx->state;
    ctx->destination[ctx->output_pos++] = value;
    state->window[state->window_pos] = value;
    state->window_pos = (state->window_pos + 1u) & XX_RARX15_WINDOW_MASK;
    if (state->window_pos == 0) state->first_window_done = true;
    return true;
}

static bool xx_rarx15_copy(xx_rarx15_ctx *ctx, uint32_t distance,
                           uint32_t length) {
    xx_rarx15_state *state;
    bool zero_fill;
    uint32_t i;
    if (!ctx) return false;
    if ((size_t)length > ctx->destination_size - ctx->output_pos) {
        return xx_rarx15_fail(ctx, XX_RARX_STATUS_CORRUPT);
    }
    state = ctx->state;
    zero_fill = distance == 0 || distance > XX_RARX15_WINDOW_SIZE ||
                (!state->first_window_done && (size_t)distance > state->window_pos);
    for (i = 0; i < length; ++i) {
        uint8_t value = 0;
        if (!zero_fill) {
            value = state->window[(state->window_pos - (size_t)distance) &
                                  XX_RARX15_WINDOW_MASK];
        }
        if (!xx_rarx15_put_byte(ctx, value)) return false;
    }
    return true;
}

static void xx_rarx15_remember_match(xx_rarx15_state *state,
                                     uint32_t distance, uint32_t length) {
    state->old_distance[state->old_distance_pos] = distance;
    state->old_distance_pos = (state->old_distance_pos + 1u) & 3u;
    state->last_distance = distance;
    state->last_length = length;
}

static uint32_t xx_rarx15_short_len1(const xx_rarx15_state *state,
                                     size_t index) {
    return index == 1 ? state->buf60 + 3u : g_short_len1[index];
}

static uint32_t xx_rarx15_short_len2(const xx_rarx15_state *state,
                                     size_t index) {
    return index == 3 ? state->buf60 + 3u : g_short_len2[index];
}

static bool xx_rarx15_get_flags(xx_rarx15_ctx *ctx) {
    xx_rarx15_state *state = ctx->state;
    uint32_t bits;
    uint32_t place_value;
    size_t place;
    uint32_t flags;
    size_t new_place;
    if (!xx_rarx15_peek16(&ctx->bits, &bits) ||
        !XX_RARX15_DECODE_NUM(ctx, bits, 5, g_dec_hf2, g_pos_hf2,
                              &place_value)) {
        return false;
    }
    if (place_value >= 256) return xx_rarx15_fail(ctx, XX_RARX_STATUS_CORRUPT);
    place = (size_t)place_value;
    for (;;) {
        flags = state->ch_set_c[place];
        new_place = state->n_to_pl_c[flags & 0xffu];
        state->n_to_pl_c[flags & 0xffu]++;
        state->flag_buf = flags >> 8;
        ++flags;
        if ((flags & 0xffu) == 0) {
            xx_rarx15_corr_huff(state->ch_set_c, state->n_to_pl_c);
        } else {
            break;
        }
    }
    state->ch_set_c[place] = state->ch_set_c[new_place];
    state->ch_set_c[new_place] = (uint16_t)flags;
    return true;
}

static bool xx_rarx15_huffman(xx_rarx15_ctx *ctx) {
    xx_rarx15_state *state = ctx->state;
    uint32_t bits;
    uint32_t place;
    size_t index;
    uint32_t current;
    size_t new_place;
    uint8_t value;
    if (!xx_rarx15_peek16(&ctx->bits, &bits))
        return xx_rarx15_fail(ctx, ctx->bits.status);

    if (state->avr_plc > 0x75ff) {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 8, g_dec_hf4, g_pos_hf4, &place))
            return false;
    } else if (state->avr_plc > 0x5dff) {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 6, g_dec_hf3, g_pos_hf3, &place))
            return false;
    } else if (state->avr_plc > 0x35ff) {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 5, g_dec_hf2, g_pos_hf2, &place))
            return false;
    } else if (state->avr_plc > 0x0dff) {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 5, g_dec_hf1, g_pos_hf1, &place))
            return false;
    } else {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 4, g_dec_hf0, g_pos_hf0, &place))
            return false;
    }
    place &= 0xffu;

    if (state->static_mode) {
        if (place == 0 && bits > 0x0fffu) place = 0x100u;
        if (place == 0) {
            uint32_t control;
            uint32_t distance_high;
            uint32_t distance_low;
            uint32_t length;
            if (!xx_rarx15_peek16(&ctx->bits, &control) ||
                !xx_rarx15_consume(&ctx->bits, 1)) {
                return xx_rarx15_fail(ctx, ctx->bits.status);
            }
            if ((control & 0x8000u) != 0) {
                state->num_huf = 0;
                state->static_mode = false;
                return true;
            }
            length = (control & 0x4000u) != 0 ? 4u : 3u;
            if (!xx_rarx15_consume(&ctx->bits, 1) ||
                !xx_rarx15_peek16(&ctx->bits, &control) ||
                !XX_RARX15_DECODE_NUM(ctx, control, 5, g_dec_hf2, g_pos_hf2,
                                      &distance_high) ||
                !xx_rarx15_peek16(&ctx->bits, &distance_low) ||
                !xx_rarx15_consume(&ctx->bits, 5)) {
                return xx_rarx15_fail(ctx, ctx->bits.status);
            }
            return xx_rarx15_copy(ctx, (distance_high << 5) |
                                        (distance_low >> 11), length);
        }
        --place;
    } else {
        if (state->num_huf >= 16 && state->flags_count == 0)
            state->static_mode = true;
        ++state->num_huf;
    }

    if (place >= 256) return xx_rarx15_fail(ctx, XX_RARX_STATUS_CORRUPT);
    state->avr_plc += place;
    state->avr_plc -= state->avr_plc >> 8;
    state->nhfb += 16;
    if (state->nhfb > 0xff) {
        state->nhfb = 0x90;
        state->nlzb >>= 1;
    }

    index = (size_t)place;
    value = (uint8_t)(state->ch_set[index] >> 8);
    if (!xx_rarx15_put_byte(ctx, value)) return false;

    for (;;) {
        current = state->ch_set[index];
        new_place = state->n_to_pl[current & 0xffu];
        state->n_to_pl[current & 0xffu]++;
        ++current;
        if ((current & 0xffu) > 0xa1u) {
            xx_rarx15_corr_huff(state->ch_set, state->n_to_pl);
        } else {
            break;
        }
    }
    state->ch_set[index] = state->ch_set[new_place];
    state->ch_set[new_place] = (uint16_t)current;
    return true;
}

static bool xx_rarx15_short_lz(xx_rarx15_ctx *ctx) {
    xx_rarx15_state *state = ctx->state;
    uint32_t bits;
    uint8_t high;
    size_t code = 0;
    uint32_t length;
    uint32_t distance;
    uint32_t decoded;
    state->num_huf = 0;
    if (!xx_rarx15_peek16(&ctx->bits, &bits))
        return xx_rarx15_fail(ctx, ctx->bits.status);

    if (state->l_count == 2) {
        if (!xx_rarx15_consume(&ctx->bits, 1))
            return xx_rarx15_fail(ctx, ctx->bits.status);
        if (bits >= 0x8000u)
            return xx_rarx15_copy(ctx, state->last_distance,
                                  state->last_length);
        bits = (bits << 1) & 0xffffu;
        state->l_count = 0;
    }

    high = (uint8_t)(bits >> 8);
    if (state->avr_ln1 < 37) {
        while (code < 15) {
            uint32_t width = xx_rarx15_short_len1(state, code);
            uint8_t mask = (uint8_t)~(0xffu >> width);
            if (((high ^ g_short_xor1[code]) & mask) == 0) break;
            ++code;
        }
        if (code == 15 ||
            !xx_rarx15_consume(&ctx->bits,
                               xx_rarx15_short_len1(state, code))) {
            return xx_rarx15_fail(ctx, code == 15 ? XX_RARX_STATUS_CORRUPT
                                                   : ctx->bits.status);
        }
    } else {
        while (code < 15) {
            uint32_t width = xx_rarx15_short_len2(state, code);
            uint8_t mask = (uint8_t)~(0xffu >> width);
            if (((high ^ g_short_xor2[code]) & mask) == 0) break;
            ++code;
        }
        if (code == 15 ||
            !xx_rarx15_consume(&ctx->bits,
                               xx_rarx15_short_len2(state, code))) {
            return xx_rarx15_fail(ctx, code == 15 ? XX_RARX_STATUS_CORRUPT
                                                   : ctx->bits.status);
        }
    }

    length = (uint32_t)code;
    if (length >= 9) {
        if (length == 9) {
            ++state->l_count;
            return xx_rarx15_copy(ctx, state->last_distance,
                                  state->last_length);
        }
        if (length == 14) {
            state->l_count = 0;
            if (!xx_rarx15_peek16(&ctx->bits, &bits) ||
                !XX_RARX15_DECODE_NUM(ctx, bits, 3, g_dec_l2, g_pos_l2,
                                      &decoded) ||
                !xx_rarx15_peek16(&ctx->bits, &bits) ||
                !xx_rarx15_consume(&ctx->bits, 15)) {
                return xx_rarx15_fail(ctx, ctx->bits.status);
            }
            length = decoded + 5u;
            distance = (bits >> 1) | 0x8000u;
            state->last_length = length;
            state->last_distance = distance;
            return xx_rarx15_copy(ctx, distance, length);
        }

        state->l_count = 0;
        distance = state->old_distance[
            (state->old_distance_pos - (size_t)(length - 9u)) & 3u];
        if (!xx_rarx15_peek16(&ctx->bits, &bits) ||
            !XX_RARX15_DECODE_NUM(ctx, bits, 2, g_dec_l1, g_pos_l1,
                                  &decoded)) {
            return false;
        }
        length = decoded + 2u;
        if (length == 0x101u && code == 10) {
            state->buf60 ^= 1u;
            return true;
        }
        if (distance > 256u) ++length;
        if (distance >= state->max_dist3) ++length;
        xx_rarx15_remember_match(state, distance, length);
        return xx_rarx15_copy(ctx, distance, length);
    }

    state->l_count = 0;
    state->avr_ln1 += length;
    state->avr_ln1 -= state->avr_ln1 >> 4;
    if (!xx_rarx15_peek16(&ctx->bits, &bits) ||
        !XX_RARX15_DECODE_NUM(ctx, bits, 5, g_dec_hf2, g_pos_hf2,
                              &decoded)) {
        return false;
    }
    decoded &= 0xffu;
    distance = state->ch_set_a[decoded];
    if (decoded != 0) {
        uint16_t previous = state->ch_set_a[decoded - 1u];
        state->ch_set_a[decoded] = previous;
        state->ch_set_a[decoded - 1u] = (uint16_t)distance;
    }
    length += 2u;
    ++distance;
    xx_rarx15_remember_match(state, distance, length);
    return xx_rarx15_copy(ctx, distance, length);
}

static bool xx_rarx15_long_lz(xx_rarx15_ctx *ctx) {
    xx_rarx15_state *state = ctx->state;
    uint32_t bits;
    uint32_t length;
    uint32_t distance_place;
    uint32_t distance;
    uint32_t old_avr2 = state->avr_ln2;
    uint32_t old_avr3;
    size_t index;
    size_t new_place;

    state->num_huf = 0;
    state->nlzb += 16;
    if (state->nlzb > 0xff) {
        state->nlzb = 0x90;
        state->nhfb >>= 1;
    }
    if (!xx_rarx15_peek16(&ctx->bits, &bits))
        return xx_rarx15_fail(ctx, ctx->bits.status);
    if (state->avr_ln2 >= 122) {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 3, g_dec_l2, g_pos_l2, &length))
            return false;
    } else if (state->avr_ln2 >= 64) {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 2, g_dec_l1, g_pos_l1, &length))
            return false;
    } else if (bits < 0x100u) {
        length = bits;
        if (!xx_rarx15_consume(&ctx->bits, 16))
            return xx_rarx15_fail(ctx, ctx->bits.status);
    } else {
        uint32_t probe = bits;
        length = 0;
        while ((probe & 0x8000u) == 0) {
            if (++length >= 16)
                return xx_rarx15_fail(ctx, XX_RARX_STATUS_CORRUPT);
            probe = (probe << 1) & 0xffffu;
        }
        if (!xx_rarx15_consume(&ctx->bits, (size_t)length + 1u))
            return xx_rarx15_fail(ctx, ctx->bits.status);
    }
    state->avr_ln2 += length;
    state->avr_ln2 -= state->avr_ln2 >> 5;

    if (!xx_rarx15_peek16(&ctx->bits, &bits))
        return xx_rarx15_fail(ctx, ctx->bits.status);
    if (state->avr_plc_b > 0x28ff) {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 5, g_dec_hf2, g_pos_hf2,
                                  &distance_place)) return false;
    } else if (state->avr_plc_b > 0x06ff) {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 5, g_dec_hf1, g_pos_hf1,
                                  &distance_place)) return false;
    } else {
        if (!XX_RARX15_DECODE_NUM(ctx, bits, 4, g_dec_hf0, g_pos_hf0,
                                  &distance_place)) return false;
    }
    state->avr_plc_b += distance_place;
    state->avr_plc_b -= state->avr_plc_b >> 8;
    index = (size_t)(distance_place & 0xffu);
    for (;;) {
        distance = state->ch_set_b[index];
        new_place = state->n_to_pl_b[distance & 0xffu];
        state->n_to_pl_b[distance & 0xffu]++;
        ++distance;
        if ((distance & 0xffu) == 0) {
            xx_rarx15_corr_huff(state->ch_set_b, state->n_to_pl_b);
        } else {
            break;
        }
    }
    state->ch_set_b[index] = state->ch_set_b[new_place];
    state->ch_set_b[new_place] = (uint16_t)distance;
    if (!xx_rarx15_peek16(&ctx->bits, &bits) ||
        !xx_rarx15_consume(&ctx->bits, 7)) {
        return xx_rarx15_fail(ctx, ctx->bits.status);
    }
    distance = ((distance & 0xff00u) | (bits >> 8)) >> 1;

    old_avr3 = state->avr_ln3;
    if (length != 1 && length != 4) {
        if (length == 0 && distance <= state->max_dist3) {
            ++state->avr_ln3;
            state->avr_ln3 -= state->avr_ln3 >> 8;
        } else if (state->avr_ln3 != 0) {
            --state->avr_ln3;
        }
    }
    length += 3;
    if (distance >= state->max_dist3) ++length;
    if (distance <= 256) length += 8;
    state->max_dist3 = old_avr3 > 0xb0 ||
                       (state->avr_plc >= 0x2a00 && old_avr2 < 0x40)
                           ? 0x7f00u : 0x2001u;
    xx_rarx15_remember_match(state, distance, length);
    return xx_rarx15_copy(ctx, distance, length);
}

static bool xx_rarx15_step(xx_rarx15_ctx *ctx) {
    xx_rarx15_state *state = ctx->state;
    if (state->flags_count == -2) {
        if (!xx_rarx15_get_flags(ctx)) return false;
        state->flags_count = 8;
    }
    if (state->static_mode) return xx_rarx15_huffman(ctx);

    --state->flags_count;
    if (state->flags_count < 0) {
        if (!xx_rarx15_get_flags(ctx)) return false;
        state->flags_count = 7;
    }
    if ((state->flag_buf & 0x80u) != 0) {
        state->flag_buf = (state->flag_buf << 1) & 0xffu;
        return state->nlzb > state->nhfb ? xx_rarx15_long_lz(ctx)
                                         : xx_rarx15_huffman(ctx);
    }

    state->flag_buf = (state->flag_buf << 1) & 0xffu;
    --state->flags_count;
    if (state->flags_count < 0) {
        if (!xx_rarx15_get_flags(ctx)) return false;
        state->flags_count = 7;
    }
    if ((state->flag_buf & 0x80u) != 0) {
        state->flag_buf = (state->flag_buf << 1) & 0xffu;
        return state->nlzb > state->nhfb ? xx_rarx15_huffman(ctx)
                                         : xx_rarx15_long_lz(ctx);
    }
    state->flag_buf = (state->flag_buf << 1) & 0xffu;
    return xx_rarx15_short_lz(ctx);
}

xx_rarx15_state *xx_rarx15_create(void) {
    xx_rarx15_state *state =
        (xx_rarx15_state *)xx_mem_calloc(1, sizeof(xx_rarx15_state));
    if (state) xx_rarx15_reset(state);
    return state;
}

void xx_rarx15_destroy(xx_rarx15_state *state) {
    xx_rarx_clear_free(state, sizeof(*state));
}

void xx_rarx15_reset(xx_rarx15_state *state) {
    if (!state) return;
    xx_rarx15_reset_non_solid(state);
    state->flags_count = -2;
}

xx_rarx_status_t xx_rarx15_decode(xx_rarx15_state *state,
                                  const uint8_t *source, size_t source_size,
                                  uint8_t *destination, size_t destination_size,
                                  size_t window_size, bool solid,
                                  size_t *source_used, xx_pd_struct *progress) {
    xx_rarx15_ctx ctx;
    if (source_used) *source_used = 0;
    if (!state || (!source && source_size != 0) ||
        (!destination && destination_size != 0) || window_size == 0) {
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    }
    if (progress && xx_pd_is_stopped(progress))
        return XX_RARX_STATUS_CANCELLED;

    if (!solid) xx_rarx15_reset_non_solid(state);
    state->flags_count = -2;
    state->flag_buf = 0;
    state->static_mode = false;
    state->l_count = 0;

    xx_rt_memset(&ctx, 0, sizeof(ctx));
    ctx.state = state;
    ctx.bits.data = source;
    ctx.bits.size = source_size;
    ctx.bits.status = XX_RARX_STATUS_OK;
    ctx.destination = destination;
    ctx.destination_size = destination_size;
    ctx.progress = progress;
    ctx.status = XX_RARX_STATUS_OK;

    while (ctx.output_pos < destination_size && ctx.status == XX_RARX_STATUS_OK) {
        if (progress && (ctx.output_pos & 0xfffu) == 0 &&
            xx_pd_is_stopped(progress)) {
            ctx.status = XX_RARX_STATUS_CANCELLED;
            break;
        }
        if (!xx_rarx15_step(&ctx)) break;
    }
    if (source_used) *source_used = xx_rarx15_used_bytes(&ctx.bits);
    if (ctx.status == XX_RARX_STATUS_OK && ctx.bits.status != XX_RARX_STATUS_OK)
        ctx.status = ctx.bits.status;
    if (ctx.status == XX_RARX_STATUS_OK &&
        ctx.output_pos != destination_size)
        ctx.status = XX_RARX_STATUS_CORRUPT;
    return ctx.status;
}
