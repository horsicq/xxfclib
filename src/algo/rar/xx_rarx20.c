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
 * Clean-room RAR 2.0 (UNP_VER 20/26) decoder based on the independent
 * rar-research format description.  It implements canonical-Huffman LZ,
 * solid continuations and the legacy four-channel adaptive audio mode.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_rarx_internal.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <string.h>

#define XX_RARX20_MAIN_COUNT 298u
#define XX_RARX20_OFFSET_COUNT 48u
#define XX_RARX20_LENGTH_COUNT 28u
#define XX_RARX20_LEVEL_COUNT 19u
#define XX_RARX20_TABLE_COUNT \
    (XX_RARX20_MAIN_COUNT + XX_RARX20_OFFSET_COUNT + XX_RARX20_LENGTH_COUNT)
#define XX_RARX20_AUDIO_COUNT 257u
#define XX_RARX20_MAX_CHANNELS 4u
#define XX_RARX20_OLD_LEVEL_COUNT \
    (XX_RARX20_AUDIO_COUNT * XX_RARX20_MAX_CHANNELS)
#define XX_RARX20_MAX_SYMBOLS XX_RARX20_MAIN_COUNT
#define XX_RARX20_WINDOW_SIZE 0x100000u
#define XX_RARX20_WINDOW_MASK 0xfffffu

static const uint16_t g_length_base[XX_RARX20_LENGTH_COUNT] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20,
    24, 28, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224
};
static const uint8_t g_length_bits[XX_RARX20_LENGTH_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2,
    2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5
};
static const uint32_t g_offset_base[XX_RARX20_OFFSET_COUNT] = {
    0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192,
    256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192,
    12288, 16384, 24576, 32768, 49152, 65536, 98304, 131072, 196608,
    262144, 327680, 393216, 458752, 524288, 589824, 655360, 720896,
    786432, 851968, 917504, 983040
};
static const uint8_t g_offset_bits[XX_RARX20_OFFSET_COUNT] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14,
    15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16
};
static const uint16_t g_short_base[8] = {0, 4, 8, 16, 32, 64, 128, 192};
static const uint8_t g_short_bits[8] = {2, 2, 3, 4, 5, 6, 6, 6};

typedef struct xx_rarx20_bits_s {
    const uint8_t *data;
    size_t size;
    size_t bit_pos;
    xx_rarx_status_t status;
} xx_rarx20_bits;

typedef struct xx_rarx20_huffman_s {
    uint16_t symbols[XX_RARX20_MAX_SYMBOLS];
    uint16_t first_code[16];
    uint16_t first_index[16];
    uint16_t counts[16];
    uint16_t symbol_count;
} xx_rarx20_huffman;

typedef struct xx_rarx20_audio_s {
    int32_t k[5];
    int32_t d1;
    int32_t d2;
    int32_t d3;
    int32_t d4;
    int32_t last_delta;
    int32_t last_char;
    uint32_t byte_count;
    uint32_t differences[11];
} xx_rarx20_audio;

struct xx_rarx20_state {
    uint8_t *window;
    size_t window_pos;
    size_t history_size;

    uint8_t levels[XX_RARX20_OLD_LEVEL_COUNT];
    xx_rarx20_huffman main_table;
    xx_rarx20_huffman offset_table;
    xx_rarx20_huffman length_table;
    xx_rarx20_huffman audio_table[XX_RARX20_MAX_CHANNELS];
    bool audio_block;
    size_t channels;
    size_t current_channel;
    xx_rarx20_audio audio[XX_RARX20_MAX_CHANNELS];
    int32_t channel_delta;

    size_t old_offset[4];
    size_t last_offset;
    size_t last_length;
    size_t pending_length;
    size_t pending_offset;
    bool pending_zero_fill;
    bool in_block;
};

typedef struct xx_rarx20_ctx_s {
    xx_rarx20_state *state;
    xx_rarx20_bits bits;
    uint8_t *destination;
    size_t destination_size;
    size_t output_pos;
    xx_pd_struct *progress;
    xx_rarx_status_t status;
} xx_rarx20_ctx;

static bool xx_rarx20_fail(xx_rarx20_ctx *ctx, xx_rarx_status_t status) {
    if (ctx && ctx->status == XX_RARX_STATUS_OK) ctx->status = status;
    return false;
}

static size_t xx_rarx20_used_bytes(const xx_rarx20_bits *bits) {
    size_t used;
    if (!bits) return 0;
    used = bits->bit_pos / 8u;
    if ((bits->bit_pos & 7u) != 0 && used < SIZE_MAX) ++used;
    return used > bits->size ? bits->size : used;
}

static size_t xx_rarx20_remaining_bits(const xx_rarx20_bits *bits) {
    size_t byte_pos;
    size_t full_bytes;
    size_t result;
    if (!bits) return 0;
    byte_pos = bits->bit_pos / 8u;
    if (byte_pos >= bits->size) return 0;
    full_bytes = bits->size - byte_pos - 1u;
    if (full_bytes > (SIZE_MAX - (8u - (bits->bit_pos & 7u))) / 8u)
        return SIZE_MAX;
    result = full_bytes * 8u + 8u - (bits->bit_pos & 7u);
    return result;
}

static bool xx_rarx20_read_bits(xx_rarx20_bits *bits, unsigned count,
                                uint32_t *value) {
    unsigned i;
    uint32_t result = 0;
    if (!bits || !value || count > 24 || bits->status != XX_RARX_STATUS_OK) {
        if (bits && bits->status == XX_RARX_STATUS_OK)
            bits->status = XX_RARX_STATUS_CORRUPT;
        return false;
    }
    if ((size_t)count > xx_rarx20_remaining_bits(bits)) {
        bits->status = XX_RARX_STATUS_TRUNCATED;
        return false;
    }
    for (i = 0; i < count; ++i) {
        size_t index = bits->bit_pos + i;
        result = (result << 1) |
                 (uint32_t)((bits->data[index / 8u] >>
                             (7u - (index & 7u))) & 1u);
    }
    bits->bit_pos += count;
    *value = result;
    return true;
}

static bool xx_rarx20_peek16(xx_rarx20_bits *bits, uint32_t *value) {
    size_t saved;
    bool ok;
    if (!bits) return false;
    saved = bits->bit_pos;
    ok = xx_rarx20_read_bits(bits, 16, value);
    bits->bit_pos = saved;
    return ok;
}

static bool xx_rarx20_build_huffman(xx_rarx20_ctx *ctx,
                                    xx_rarx20_huffman *table,
                                    const uint8_t *lengths,
                                    size_t length_count) {
    uint32_t available = 1;
    uint32_t code = 0;
    size_t length;
    size_t symbol;
    size_t out_pos = 0;
    if (!ctx || !table || !lengths ||
        length_count > XX_RARX20_MAX_SYMBOLS) {
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    }
    xx_rt_memset(table, 0, sizeof(*table));
    for (symbol = 0; symbol < length_count; ++symbol) {
        if (lengths[symbol] > 15)
            return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
        if (lengths[symbol] != 0) ++table->counts[lengths[symbol]];
    }
    for (length = 1; length <= 15; ++length) {
        available <<= 1;
        if (table->counts[length] > available)
            return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
        available -= table->counts[length];
    }
    for (length = 1; length <= 15; ++length) {
        code = (code + table->counts[length - 1u]) << 1;
        if (code > UINT16_MAX)
            return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
        table->first_code[length] = (uint16_t)code;
        table->first_index[length] = (uint16_t)out_pos;
        out_pos += table->counts[length];
    }
    if (out_pos > XX_RARX20_MAX_SYMBOLS)
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    out_pos = 0;
    for (length = 1; length <= 15; ++length) {
        for (symbol = 0; symbol < length_count; ++symbol) {
            if (lengths[symbol] == length)
                table->symbols[out_pos++] = (uint16_t)symbol;
        }
    }
    table->symbol_count = (uint16_t)out_pos;
    return true;
}

static bool xx_rarx20_huffman_symbol(xx_rarx20_ctx *ctx,
                                     const xx_rarx20_huffman *table,
                                     size_t *symbol) {
    uint32_t code = 0;
    size_t length;
    if (!ctx || !table || !symbol || table->symbol_count == 0)
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    for (length = 1; length <= 15; ++length) {
        uint32_t bit;
        uint32_t first;
        uint32_t offset;
        if (!xx_rarx20_read_bits(&ctx->bits, 1, &bit))
            return xx_rarx20_fail(ctx, ctx->bits.status);
        code = (code << 1) | bit;
        if (table->counts[length] == 0) continue;
        first = table->first_code[length];
        if (code < first) continue;
        offset = code - first;
        if (offset < table->counts[length]) {
            size_t index = table->first_index[length] + (size_t)offset;
            if (index >= table->symbol_count)
                return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
            *symbol = table->symbols[index];
            return true;
        }
    }
    return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
}

static bool xx_rarx20_fill_levels(xx_rarx20_ctx *ctx, uint8_t *levels,
                                  size_t *position, size_t count,
                                  uint8_t value) {
    size_t end;
    size_t i;
    if (!levels || !position || *position > XX_RARX20_OLD_LEVEL_COUNT ||
        count > SIZE_MAX - *position)
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    end = *position + count;
    if (end > XX_RARX20_OLD_LEVEL_COUNT)
        end = XX_RARX20_OLD_LEVEL_COUNT;
    for (i = *position; i < end; ++i) levels[i] = value;
    *position = end;
    return true;
}

static bool xx_rarx20_read_tables(xx_rarx20_ctx *ctx) {
    xx_rarx20_state *state = ctx->state;
    uint8_t level_lengths[XX_RARX20_LEVEL_COUNT];
    uint8_t new_levels[XX_RARX20_OLD_LEVEL_COUNT];
    xx_rarx20_huffman level_table;
    uint32_t bit_field;
    uint32_t ignored;
    bool keep_tables;
    size_t table_size;
    size_t position = 0;
    size_t i;

    if (!xx_rarx20_peek16(&ctx->bits, &bit_field) ||
        !xx_rarx20_read_bits(&ctx->bits, 2, &ignored))
        return xx_rarx20_fail(ctx, ctx->bits.status);
    state->audio_block = (bit_field & 0x8000u) != 0;
    keep_tables = (bit_field & 0x4000u) != 0;
    if (!keep_tables) xx_rt_memset(state->levels, 0, sizeof(state->levels));

    if (state->audio_block) {
        state->channels = ((bit_field >> 12) & 3u) + 1u;
        if (state->current_channel >= state->channels)
            state->current_channel = 0;
        if (!xx_rarx20_read_bits(&ctx->bits, 2, &ignored))
            return xx_rarx20_fail(ctx, ctx->bits.status);
        table_size = XX_RARX20_AUDIO_COUNT * state->channels;
    } else {
        table_size = XX_RARX20_TABLE_COUNT;
    }

    for (i = 0; i < XX_RARX20_LEVEL_COUNT; ++i) {
        uint32_t level;
        if (!xx_rarx20_read_bits(&ctx->bits, 4, &level))
            return xx_rarx20_fail(ctx, ctx->bits.status);
        level_lengths[i] = (uint8_t)level;
    }
    if (!xx_rarx20_build_huffman(ctx, &level_table, level_lengths,
                                  XX_RARX20_LEVEL_COUNT)) return false;
    xx_rt_memset(new_levels, 0, sizeof(new_levels));
    while (position < table_size) {
        size_t level_symbol;
        if (!xx_rarx20_huffman_symbol(ctx, &level_table, &level_symbol))
            return false;
        if (level_symbol <= 15) {
            new_levels[position] =
                (uint8_t)((state->levels[position] + level_symbol) & 0x0fu);
            ++position;
        } else if (level_symbol == 16) {
            uint32_t extra;
            if (position == 0)
                return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
            if (!xx_rarx20_read_bits(&ctx->bits, 2, &extra) ||
                !xx_rarx20_fill_levels(ctx, new_levels, &position,
                                       3u + extra,
                                       new_levels[position - 1u]))
                return false;
        } else if (level_symbol == 17) {
            uint32_t extra;
            if (!xx_rarx20_read_bits(&ctx->bits, 3, &extra) ||
                !xx_rarx20_fill_levels(ctx, new_levels, &position,
                                       3u + extra, 0)) return false;
        } else if (level_symbol == 18) {
            uint32_t extra;
            if (!xx_rarx20_read_bits(&ctx->bits, 7, &extra) ||
                !xx_rarx20_fill_levels(ctx, new_levels, &position,
                                       11u + extra, 0)) return false;
        } else {
            return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
        }
    }
    xx_rt_memcpy(state->levels, new_levels, sizeof(state->levels));

    if (state->audio_block) {
        for (i = 0; i < state->channels; ++i) {
            if (!xx_rarx20_build_huffman(
                    ctx, &state->audio_table[i],
                    state->levels + i * XX_RARX20_AUDIO_COUNT,
                    XX_RARX20_AUDIO_COUNT)) return false;
        }
    } else {
        if (!xx_rarx20_build_huffman(ctx, &state->main_table,
                                     state->levels, XX_RARX20_MAIN_COUNT) ||
            !xx_rarx20_build_huffman(
                ctx, &state->offset_table,
                state->levels + XX_RARX20_MAIN_COUNT,
                XX_RARX20_OFFSET_COUNT) ||
            !xx_rarx20_build_huffman(
                ctx, &state->length_table,
                state->levels + XX_RARX20_MAIN_COUNT + XX_RARX20_OFFSET_COUNT,
                XX_RARX20_LENGTH_COUNT)) return false;
    }
    return true;
}

static bool xx_rarx20_put_byte(xx_rarx20_ctx *ctx, uint8_t value) {
    xx_rarx20_state *state;
    if (!ctx || ctx->output_pos >= ctx->destination_size)
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    if (ctx->progress && (ctx->output_pos & 0xfffu) == 0 &&
        xx_pd_is_stopped(ctx->progress))
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CANCELLED);
    state = ctx->state;
    ctx->destination[ctx->output_pos++] = value;
    state->window[state->window_pos] = value;
    state->window_pos = (state->window_pos + 1u) & XX_RARX20_WINDOW_MASK;
    if (state->history_size < XX_RARX20_WINDOW_SIZE) ++state->history_size;
    return true;
}

static bool xx_rarx20_copy_match_part(xx_rarx20_ctx *ctx, size_t length,
                                      size_t offset, bool zero_fill) {
    xx_rarx20_state *state = ctx->state;
    size_t i;
    if (offset == 0) offset = 1;
    for (i = 0; i < length; ++i) {
        uint8_t value;
        if (ctx->output_pos >= ctx->destination_size) {
            state->pending_length = length - i;
            state->pending_offset = offset;
            state->pending_zero_fill = zero_fill;
            return true;
        }
        value = zero_fill
                    ? 0
                    : state->window[(state->window_pos - offset) &
                                    XX_RARX20_WINDOW_MASK];
        if (!xx_rarx20_put_byte(ctx, value)) return false;
    }
    return true;
}

static bool xx_rarx20_copy_match(xx_rarx20_ctx *ctx, size_t length,
                                 size_t offset) {
    bool zero_fill;
    if (offset == 0) offset = 1;
    if (offset > XX_RARX20_WINDOW_SIZE)
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    zero_fill = offset > ctx->state->history_size;
    return xx_rarx20_copy_match_part(ctx, length, offset, zero_fill);
}

static bool xx_rarx20_drain_pending(xx_rarx20_ctx *ctx) {
    xx_rarx20_state *state = ctx->state;
    size_t length;
    size_t offset;
    bool zero_fill;
    if (state->pending_length == 0) return true;
    length = state->pending_length;
    offset = state->pending_offset;
    zero_fill = state->pending_zero_fill;
    state->pending_length = 0;
    return xx_rarx20_copy_match_part(ctx, length, offset, zero_fill);
}

static void xx_rarx20_push_offset(xx_rarx20_state *state, size_t offset) {
    state->old_offset[3] = state->old_offset[2];
    state->old_offset[2] = state->old_offset[1];
    state->old_offset[1] = state->old_offset[0];
    state->old_offset[0] = offset;
}

static uint32_t xx_rarx20_abs_i32(int32_t value) {
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

static int32_t xx_rarx20_signed_byte(uint8_t value) {
    return value < 0x80u ? (int32_t)value : (int32_t)value - 0x100;
}

/* The legacy predictor specifies an arithmetic shift.  Spell it out instead
 * of relying on implementation-defined right shift of a negative value. */
static int32_t xx_rarx20_sar3(int32_t value) {
    if (value >= 0) return value / 8;
    return -(int32_t)(((uint32_t)(-(int64_t)value) + 7u) / 8u);
}

static uint8_t xx_rarx20_decode_audio_value(xx_rarx20_state *state,
                                            uint8_t delta) {
    xx_rarx20_audio *audio = &state->audio[state->current_channel];
    int32_t predicted;
    int32_t d;
    uint8_t value;
    size_t i;
    size_t best = 0;
    uint32_t minimum;

    ++audio->byte_count;
    audio->d4 = audio->d3;
    audio->d3 = audio->d2;
    audio->d2 = audio->last_delta - audio->d1;
    audio->d1 = audio->last_delta;
    predicted = 8 * audio->last_char +
                audio->k[0] * audio->d1 + audio->k[1] * audio->d2 +
                audio->k[2] * audio->d3 + audio->k[3] * audio->d4 +
                audio->k[4] * state->channel_delta;
    predicted = (int32_t)((uint32_t)xx_rarx20_sar3(predicted) & 0xffu);
    value = (uint8_t)(predicted - (int32_t)delta);
    d = xx_rarx20_signed_byte(delta) * 8;

    audio->differences[0] += xx_rarx20_abs_i32(d);
    audio->differences[1] += xx_rarx20_abs_i32(d - audio->d1);
    audio->differences[2] += xx_rarx20_abs_i32(d + audio->d1);
    audio->differences[3] += xx_rarx20_abs_i32(d - audio->d2);
    audio->differences[4] += xx_rarx20_abs_i32(d + audio->d2);
    audio->differences[5] += xx_rarx20_abs_i32(d - audio->d3);
    audio->differences[6] += xx_rarx20_abs_i32(d + audio->d3);
    audio->differences[7] += xx_rarx20_abs_i32(d - audio->d4);
    audio->differences[8] += xx_rarx20_abs_i32(d + audio->d4);
    audio->differences[9] += xx_rarx20_abs_i32(d - state->channel_delta);
    audio->differences[10] += xx_rarx20_abs_i32(d + state->channel_delta);

    state->channel_delta = xx_rarx20_signed_byte(
        (uint8_t)(value - (uint8_t)audio->last_char));
    audio->last_delta = state->channel_delta;
    audio->last_char = value;

    if ((audio->byte_count & 0x1fu) == 0) {
        minimum = audio->differences[0];
        audio->differences[0] = 0;
        for (i = 1; i < 11; ++i) {
            if (audio->differences[i] < minimum) {
                minimum = audio->differences[i];
                best = i;
            }
            audio->differences[i] = 0;
        }
        if (best != 0) {
            size_t coefficient = (best - 1u) / 2u;
            if ((best & 1u) != 0) {
                if (audio->k[coefficient] >= -16) --audio->k[coefficient];
            } else if (audio->k[coefficient] < 16) {
                ++audio->k[coefficient];
            }
        }
    }
    return value;
}

static bool xx_rarx20_decode_audio(xx_rarx20_ctx *ctx) {
    xx_rarx20_state *state = ctx->state;
    size_t symbol;
    uint8_t value;
    if (state->channels == 0 || state->channels > XX_RARX20_MAX_CHANNELS ||
        state->current_channel >= state->channels)
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    if (!xx_rarx20_huffman_symbol(ctx,
                                  &state->audio_table[state->current_channel],
                                  &symbol)) return false;
    if (symbol == 256) {
        state->in_block = false;
        return true;
    }
    if (symbol > 255)
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    value = xx_rarx20_decode_audio_value(state, (uint8_t)symbol);
    if (!xx_rarx20_put_byte(ctx, value)) return false;
    ++state->current_channel;
    if (state->current_channel == state->channels)
        state->current_channel = 0;
    return true;
}

static bool xx_rarx20_read_offset(xx_rarx20_ctx *ctx, size_t *offset) {
    size_t slot;
    uint32_t extra = 0;
    if (!xx_rarx20_huffman_symbol(ctx, &ctx->state->offset_table, &slot))
        return false;
    if (slot >= XX_RARX20_OFFSET_COUNT)
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    if (g_offset_bits[slot] != 0 &&
        !xx_rarx20_read_bits(&ctx->bits, g_offset_bits[slot], &extra))
        return xx_rarx20_fail(ctx, ctx->bits.status);
    *offset = (size_t)g_offset_base[slot] + 1u + extra;
    if (*offset > XX_RARX20_WINDOW_SIZE)
        return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
    return true;
}

static bool xx_rarx20_decode_lz_symbol(xx_rarx20_ctx *ctx) {
    xx_rarx20_state *state = ctx->state;
    size_t symbol;
    size_t offset;
    size_t length;
    if (!xx_rarx20_huffman_symbol(ctx, &state->main_table, &symbol))
        return false;
    if (symbol <= 255) return xx_rarx20_put_byte(ctx, (uint8_t)symbol);

    if (symbol == 256) {
        if (state->last_length != 0) {
            xx_rarx20_push_offset(state, state->last_offset);
            return xx_rarx20_copy_match(ctx, state->last_length,
                                        state->last_offset);
        }
        return true;
    }
    if (symbol <= 260) {
        size_t length_slot;
        uint32_t extra = 0;
        offset = state->old_offset[symbol - 257u];
        if (!xx_rarx20_huffman_symbol(ctx, &state->length_table,
                                      &length_slot)) return false;
        if (length_slot >= XX_RARX20_LENGTH_COUNT)
            return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
        length = (size_t)g_length_base[length_slot] + 2u;
        if (g_length_bits[length_slot] != 0 &&
            !xx_rarx20_read_bits(&ctx->bits, g_length_bits[length_slot],
                                 &extra))
            return xx_rarx20_fail(ctx, ctx->bits.status);
        length += extra;
        if (offset >= 0x101u) ++length;
        if (offset >= 0x2000u) ++length;
        if (offset >= 0x40000u) ++length;
        xx_rarx20_push_offset(state, offset);
        state->last_offset = offset;
        state->last_length = length;
        return xx_rarx20_copy_match(ctx, length, offset);
    }
    if (symbol <= 268) {
        size_t slot = symbol - 261u;
        uint32_t extra = 0;
        offset = (size_t)g_short_base[slot] + 1u;
        if (g_short_bits[slot] != 0 &&
            !xx_rarx20_read_bits(&ctx->bits, g_short_bits[slot], &extra))
            return xx_rarx20_fail(ctx, ctx->bits.status);
        offset += extra;
        xx_rarx20_push_offset(state, offset);
        state->last_offset = offset;
        state->last_length = 2;
        return xx_rarx20_copy_match(ctx, 2, offset);
    }
    if (symbol == 269) {
        state->in_block = false;
        return true;
    }
    if (symbol < XX_RARX20_MAIN_COUNT) {
        size_t length_slot = symbol - 270u;
        uint32_t extra = 0;
        length = (size_t)g_length_base[length_slot] + 3u;
        if (g_length_bits[length_slot] != 0 &&
            !xx_rarx20_read_bits(&ctx->bits, g_length_bits[length_slot],
                                 &extra))
            return xx_rarx20_fail(ctx, ctx->bits.status);
        length += extra;
        if (!xx_rarx20_read_offset(ctx, &offset)) return false;
        if (offset >= 0x2000u) ++length;
        if (offset >= 0x40000u) ++length;
        xx_rarx20_push_offset(state, offset);
        state->last_offset = offset;
        state->last_length = length;
        return xx_rarx20_copy_match(ctx, length, offset);
    }
    return xx_rarx20_fail(ctx, XX_RARX_STATUS_CORRUPT);
}

/* Consume a trailing block marker and its following tables when they reside
 * in the current member.  This is what makes a later solid member able to
 * begin directly with symbols encoded by those tables. */
static bool xx_rarx20_read_trailing_tables(xx_rarx20_ctx *ctx) {
    xx_rarx20_state *state = ctx->state;
    size_t saved_pos;
    size_t symbol;
    bool marker;
    if (!state->in_block || xx_rarx20_remaining_bits(&ctx->bits) < 40)
        return true;
    saved_pos = ctx->bits.bit_pos;
    if (state->audio_block) {
        if (state->current_channel >= state->channels ||
            !xx_rarx20_huffman_symbol(
                ctx, &state->audio_table[state->current_channel], &symbol))
            return false;
        marker = symbol == 256;
    } else {
        if (!xx_rarx20_huffman_symbol(ctx, &state->main_table, &symbol))
            return false;
        marker = symbol == 269;
    }
    if (!marker) {
        ctx->bits.bit_pos = saved_pos;
        return true;
    }
    state->in_block = false;
    if (!xx_rarx20_read_tables(ctx)) return false;
    state->in_block = true;
    return true;
}

static void xx_rarx20_reset_non_solid(xx_rarx20_state *state) {
    uint8_t *window;
    if (!state) return;
    window = state->window;
    xx_mem_zero(window, XX_RARX20_WINDOW_SIZE);
    xx_mem_zero(state, sizeof(*state));
    state->window = window;
    state->channels = 1;
}

xx_rarx20_state *xx_rarx20_create(void) {
    xx_rarx20_state *state =
        (xx_rarx20_state *)xx_mem_calloc(1, sizeof(xx_rarx20_state));
    if (!state) return NULL;
    state->window = (uint8_t *)xx_mem_calloc(1, XX_RARX20_WINDOW_SIZE);
    if (!state->window) {
        xx_rarx_clear_free(state, sizeof(*state));
        return NULL;
    }
    xx_rarx20_reset_non_solid(state);
    return state;
}

void xx_rarx20_destroy(xx_rarx20_state *state) {
    if (!state) return;
    xx_rarx_clear_free(state->window, XX_RARX20_WINDOW_SIZE);
    xx_rarx_clear_free(state, sizeof(*state));
}

void xx_rarx20_reset(xx_rarx20_state *state) {
    xx_rarx20_reset_non_solid(state);
}

xx_rarx_status_t xx_rarx20_decode(xx_rarx20_state *state,
                                  const uint8_t *source, size_t source_size,
                                  uint8_t *destination, size_t destination_size,
                                  size_t window_size, bool solid,
                                  size_t *source_used, xx_pd_struct *progress) {
    xx_rarx20_ctx ctx;
    if (source_used) *source_used = 0;
    if (!state || !state->window || (!source && source_size != 0) ||
        (!destination && destination_size != 0) || window_size == 0)
        return XX_RARX_STATUS_INVALID_ARGUMENT;
    if (progress && xx_pd_is_stopped(progress))
        return XX_RARX_STATUS_CANCELLED;
    if (!solid) xx_rarx20_reset_non_solid(state);

    xx_rt_memset(&ctx, 0, sizeof(ctx));
    ctx.state = state;
    ctx.bits.data = source;
    ctx.bits.size = source_size;
    ctx.bits.status = XX_RARX_STATUS_OK;
    ctx.destination = destination;
    ctx.destination_size = destination_size;
    ctx.progress = progress;
    ctx.status = XX_RARX_STATUS_OK;

    while (ctx.output_pos < destination_size &&
           ctx.status == XX_RARX_STATUS_OK) {
        if (progress && (ctx.output_pos & 0xfffu) == 0 &&
            xx_pd_is_stopped(progress)) {
            ctx.status = XX_RARX_STATUS_CANCELLED;
            break;
        }
        if (!xx_rarx20_drain_pending(&ctx)) break;
        if (ctx.output_pos >= destination_size) break;
        if (!state->in_block) {
            if (!xx_rarx20_read_tables(&ctx)) break;
            state->in_block = true;
        }
        if (state->audio_block) {
            if (!xx_rarx20_decode_audio(&ctx)) break;
        } else if (!xx_rarx20_decode_lz_symbol(&ctx)) {
            break;
        }
    }
    if (ctx.status == XX_RARX_STATUS_OK &&
        ctx.output_pos == destination_size &&
        !xx_rarx20_read_trailing_tables(&ctx)) {
        /* status is set by the failed helper */
    }
    if (source_used) *source_used = xx_rarx20_used_bytes(&ctx.bits);
    if (ctx.status == XX_RARX_STATUS_OK && ctx.bits.status != XX_RARX_STATUS_OK)
        ctx.status = ctx.bits.status;
    if (ctx.status == XX_RARX_STATUS_OK &&
        ctx.output_pos != destination_size)
        ctx.status = XX_RARX_STATUS_CORRUPT;
    return ctx.status;
}
