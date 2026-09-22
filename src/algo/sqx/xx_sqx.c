/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SQX member codec (methods 1..4).  Ported from the XArchive reference
 * decoder Algos/xsqxdecoder.cpp; the algorithm, the table contents and the
 * order of every side effect are kept as they are there.
 */
#include "xxfclib/algo/sqx/xx_sqx.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define SQX_NMAIN 0x136
#define SQX_NPRE 0x13
#define SQX_NLEN 0x19
#define SQX_NDIST 0x38
#define SQX_MAXLEN_PRE 7
#define SQX_MAXLEN 15
#define SQX_MAX_DICT 0x400000
#define SQX_OVERSHOOT 0x10000

/* Distance base/extra for the len == 2 symbol class. */
static const int32_t sqx_l2base[8] = {0, 4, 8, 16, 32, 64, 128, 192};
static const int32_t sqx_l2extra[8] = {2, 2, 3, 4, 5, 6, 6, 6};
/* Distance base/extra for the len == 3 symbol class. */
static const int32_t sqx_l3base[15] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
static const int32_t sqx_l3extra[15] = {0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
/* Match length base/extra.  The two trailing zero slots are deliberate: the
 * reference indexes this table with a symbol class that never reaches them,
 * and keeping them makes the bound check read the same way. */
static const int32_t sqx_lenbase[26] = {0,  1,  2,  3,  4,  5,   6,   7,   8,   10,  12,  16, 20,
                                        24, 32, 40, 48, 64, 80,  96,  128, 160, 192, 224, 0,  0};
static const int32_t sqx_lenextra[26] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5, 5, 0, 0};
/* Distance base/extra, dictionary < 2 MiB. */
static const int32_t sqx_d48base[49] = {0,      1,      2,      3,      4,      6,      8,      12,     16,     24,     32,     48,     64,
                                        96,     128,    192,    256,    384,    512,    768,    1024,   1536,   2048,   3072,   4096,   6144,
                                        8192,   12288,  16384,  24576,  32768,  49152,  65536,  98304,  131072, 196608, 262144, 327680, 393216,
                                        458752, 524288, 589824, 655360, 720896, 786432, 851968, 917504, 983040, 1048576};
static const int32_t sqx_d48extra[49] = {0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,
                                         7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
                                         16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 0};
/* Distance base/extra, dictionary == 2 MiB. */
static const int32_t sqx_d50base[51] = {0,       1,       2,       3,       4,       6,       8,       12,      16,      24,     32,
                                        48,      64,      96,      128,     192,     256,     384,     512,     768,     1024,   1536,
                                        2048,    3072,    4096,    6144,    8192,    12288,   16384,   24576,   32768,   49152,  65536,
                                        98304,   131072,  196608,  262144,  393216,  524288,  655360,  786432,  917504,  1048576, 1179648,
                                        1310720, 1441792, 1572864, 1703936, 1835008, 1966080, 2097152};
static const int32_t sqx_d50extra[51] = {0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,
                                         7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
                                         16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 0};
/* Distance base/extra, dictionary == 4 MiB. */
static const int32_t sqx_d52base[53] = {0,       1,       2,       3,       4,       6,       8,       12,      16,      24,      32,
                                        48,      64,      96,      128,     192,     256,     384,     512,     768,     1024,    1536,
                                        2048,    3072,    4096,    6144,    8192,    12288,   16384,   24576,   32768,   49152,   65536,
                                        98304,   131072,  196608,  262144,  393216,  524288,  786432,  1048576, 1310720, 1572864, 1835008,
                                        2097152, 2359296, 2621440, 2883584, 3145728, 3407872, 3670016, 3932160, 4194304};
static const int32_t sqx_d52extra[53] = {0,  0,  0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,
                                         8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16,
                                         17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 0};

/* ------------------------------------------------------------ bit reader -- */
/* MSB-first inside little-endian 32-bit words.  The reference copies the
 * member into a buffer rounded up to a whole word plus 16 slack bytes and
 * reads zeros out of the slack, so a code that straddles the last word still
 * decodes; that padding is reproduced here. */
typedef struct sqx_bits {
    const uint8_t *words;
    int64_t word_count;
    int64_t bit_offset;
} sqx_bits;

static uint32_t sqx_read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool sqx_peek(sqx_bits *bits, int32_t count, uint32_t *value)
{
    int64_t word;
    int32_t shift;
    uint32_t acc;
    const uint8_t *p;

    word = bits->bit_offset >> 5;
    shift = (int32_t)(bits->bit_offset & 31);
    if ((word < 0) || ((word + 1) >= bits->word_count)) return false;
    p = bits->words + word * 4;
    acc = sqx_read32(p) << shift;
    if (shift) acc |= sqx_read32(p + 4) >> (32 - shift);
    *value = acc >> ((32 - count) & 31);

    return true;
}

static void sqx_skip(sqx_bits *bits, int32_t count)
{
    bits->bit_offset += count;
}

static bool sqx_get(sqx_bits *bits, int32_t count, uint32_t *value)
{
    if (!sqx_peek(bits, count, value)) return false;
    bits->bit_offset += count;

    return true;
}

/* --------------------------------------------------------------- huffman -- */
typedef struct sqx_huff {
    int32_t count;
    int32_t max_len;
    int32_t *lengths; /* count entries */
    uint16_t *lut;    /* 1 << max_len entries */
} sqx_huff;

static void sqx_huff_release(sqx_huff *huff)
{
    if (huff->lengths) xx_mem_free(huff->lengths);
    if (huff->lut) xx_mem_free(huff->lut);
    huff->lengths = 0;
    huff->lut = 0;
    huff->count = 0;
    huff->max_len = 0;
}

static bool sqx_huff_init(sqx_huff *huff, int32_t count, int32_t max_len)
{
    size_t lut_size;

    huff->lengths = 0;
    huff->lut = 0;
    huff->count = count;
    huff->max_len = max_len;
    lut_size = (size_t)1 << max_len;
    huff->lengths = (int32_t *)xx_mem_alloc(sizeof(int32_t) * (size_t)count);
    huff->lut = (uint16_t *)xx_mem_alloc(sizeof(uint16_t) * lut_size);
    if (!huff->lengths || !huff->lut) {
        sqx_huff_release(huff);
        return false;
    }
    xx_rt_memset(huff->lengths, 0, sizeof(int32_t) * (size_t)count);
    xx_rt_memset(huff->lut, 0, sizeof(uint16_t) * lut_size);

    return true;
}

static void sqx_huff_set_length(sqx_huff *huff, int32_t index, int32_t length)
{
    if ((index >= 0) && (index < huff->count)) huff->lengths[index] = length;
}

static bool sqx_huff_build(sqx_huff *huff)
{
    int32_t counts[17];
    int32_t start[18];
    int32_t step[18];
    int32_t i;
    int32_t length;
    int32_t symbol;
    int32_t lut_size;

    xx_rt_memset(counts, 0, sizeof(counts));
    xx_rt_memset(start, 0, sizeof(start));
    xx_rt_memset(step, 0, sizeof(step));
    lut_size = 1 << huff->max_len;

    for (i = 0; i < huff->count; i++) {
        length = huff->lengths[i];
        if ((length < 0) || (length > 16)) return false;
        counts[length]++;
    }
    /* The running-start arithmetic is deliberately 16-bit, as in the
     * reference: the completeness test compares against
     * (1 << max_len) & 0xFFFF.  Do not "fix" this to 32-bit. */
    for (length = 1; length <= huff->max_len; length++) {
        start[length + 1] = (start[length] + (counts[length] << (huff->max_len - length))) & 0xffff;
    }
    if (start[huff->max_len + 1] != (lut_size & 0xffff)) return false;

    for (length = 1; length <= huff->max_len; length++) {
        step[length] = 1 << (huff->max_len - length);
    }

    xx_rt_memset(huff->lut, 0, sizeof(uint16_t) * (size_t)lut_size);
    for (symbol = 0; symbol < huff->count; symbol++) {
        int32_t from;
        int32_t to;
        int32_t k;
        length = huff->lengths[symbol];
        if (!length) continue;
        from = start[length];
        to = from + step[length];
        if ((from < 0) || (to > lut_size)) return false;
        for (k = from; k < to; k++) huff->lut[k] = (uint16_t)symbol;
        start[length] = to;
    }

    return true;
}

static bool sqx_huff_decode(sqx_huff *huff, sqx_bits *bits, int32_t *symbol_out)
{
    uint32_t index;
    int32_t symbol;
    int32_t length;

    index = 0;
    if (!sqx_peek(bits, huff->max_len, &index)) return false;
    if (index >= (uint32_t)(1 << huff->max_len)) return false;
    symbol = (int32_t)huff->lut[index];
    if (symbol >= huff->count) return false;
    length = huff->lengths[symbol];
    if (length == 0) return false;
    sqx_skip(bits, length);
    *symbol_out = symbol;

    return true;
}

/* Deflate-style RLE over the 19-symbol pre-table. */
static bool sqx_read_lengths_pre(sqx_bits *bits, sqx_huff *pre, sqx_huff *table)
{
    int32_t count;
    int32_t i;

    count = table->count;
    i = 0;
    while (i < count) {
        int32_t symbol = 0;
        if (!sqx_huff_decode(pre, bits, &symbol)) return false;
        if (symbol < 0x10) {
            table->lengths[i] = symbol;
            i++;
        } else if (symbol == 0x10) {
            uint32_t extra = 0;
            int32_t repeat;
            /* The two extra bits are consumed BEFORE the "no previous entry"
             * test, exactly as in the reference. */
            if (!sqx_get(bits, 2, &extra)) return false;
            repeat = (int32_t)extra + 3;
            if (i == 0) return false;
            while ((i < count) && repeat) {
                table->lengths[i] = table->lengths[i - 1];
                i++;
                repeat--;
            }
        } else {
            uint32_t extra = 0;
            int32_t repeat = 0;
            if (symbol == 0x11) {
                if (!sqx_get(bits, 3, &extra)) return false;
                repeat = (int32_t)extra + 3;
            } else {
                if (!sqx_get(bits, 7, &extra)) return false;
                repeat = (int32_t)extra + 11;
            }
            while ((i < count) && repeat) {
                table->lengths[i] = 0;
                i++;
                repeat--;
            }
        }
    }

    return sqx_huff_build(table);
}

/* 0 same, 10 +1, 111 abs4, 1100 -1, 1101 +2. */
static bool sqx_read_lengths_delta(sqx_bits *bits, sqx_huff *table)
{
    int32_t count;
    int32_t current;
    int32_t i;
    uint32_t bit;
    uint32_t value;

    count = table->count;
    bit = 0;
    value = 0;
    if (!sqx_get(bits, 4, &value)) return false;
    current = (int32_t)value;
    table->lengths[0] = current;

    for (i = 1; i < count; i++) {
        if (!sqx_get(bits, 1, &bit)) return false;
        if (bit == 0) {
            table->lengths[i] = current;
            continue;
        }
        if (!sqx_get(bits, 1, &bit)) return false;
        if (bit == 0) {
            current += 1;
            table->lengths[i] = current;
            continue;
        }
        if (!sqx_get(bits, 1, &bit)) return false;
        if (bit == 1) {
            if (!sqx_get(bits, 4, &value)) return false;
            current = (int32_t)value;
            table->lengths[i] = current;
            continue;
        }
        if (!sqx_get(bits, 1, &bit)) return false;
        if (bit == 0) {
            current -= 1;
            if (current < 0) return false;
        } else {
            current += 2;
        }
        table->lengths[i] = current;
    }

    return sqx_huff_build(table);
}

/* ----------------------------------------------------------------- state -- */
typedef struct sqx_state {
    uint8_t *window; /* always SQX_MAX_DICT bytes */
    int32_t dict_size;
    int32_t position;
    int64_t last_length;
    int32_t last_distance;
    uint32_t rep_index;
    int32_t rep[4];
    uint8_t *out; /* NULL while a member is only replayed for its window */
    size_t out_capacity;
    int64_t produced;
    sqx_huff pre;
    sqx_huff table_a;
    sqx_huff table_b;
    sqx_huff table_len;
    sqx_huff table_dist;
} sqx_state;

/* A replayed member only has to rebuild the window, so `out` may be NULL and
 * the bytes are then counted rather than kept.  When `out` is present the
 * reference appends into a growing array and truncates at the end, so bytes
 * past out_capacity are counted and dropped here -- same result. */
static void sqx_put(sqx_state *state, uint8_t byte)
{
    state->window[state->position] = byte;
    if (state->out && ((uint64_t)state->produced < (uint64_t)state->out_capacity)) {
        state->out[state->produced] = byte;
    }
    state->produced++;
    state->position++;
    if (state->position >= state->dict_size) state->position = 0;
}

static void sqx_copy(sqx_state *state, int64_t length, int32_t distance)
{
    int32_t mask;
    int32_t position;
    int64_t i;

    state->rep[state->rep_index & 3] = distance;
    state->rep_index++;
    mask = state->dict_size - 1;
    position = state->position;
    for (i = 0; i < length; i++) {
        /* The reference does not test the back-reference against the number
         * of bytes produced so far: the window is a fixed 4 MiB buffer and
         * the index is masked, so a reference before the start of output
         * reads the window's zero fill (or, for a solid member, the
         * preceding member's bytes -- which is the point).  Matched
         * deliberately; the masked index is always in range. */
        uint8_t byte = state->window[(position - distance) & mask];
        state->window[position] = byte;
        if (state->out && ((uint64_t)(state->produced + i) < (uint64_t)state->out_capacity)) {
            state->out[state->produced + i] = byte;
        }
        position++;
        if (position >= state->dict_size) position = 0;
    }
    state->position = position;
    state->produced += length;
}

static bool sqx_read_block_header(sqx_state *state, sqx_bits *bits, int32_t *count_out, int32_t *mode_out, int32_t *threshold_out)
{
    uint32_t bit;
    uint32_t value;
    int32_t i;

    bit = 0;
    value = 0;
    if (!sqx_get(bits, 1, &bit)) return false;
    /* Kind 1 is the second ("direct") coder; it is not implemented and the
     * member fails rather than producing plausible garbage. */
    if (bit != 0) return false;

    if (!sqx_get(bits, 15, &value)) return false;
    *count_out = (int32_t)value;
    if (!sqx_get(bits, 1, &bit)) return false;
    *mode_out = (int32_t)bit;

    if (*mode_out == 1) {
        if (!sqx_get(bits, 9, &value)) return false;
        *threshold_out = (int32_t)value;
        for (i = 0; i < SQX_NPRE; i++) {
            if (!sqx_get(bits, 4, &value)) return false;
            sqx_huff_set_length(&state->pre, i, (int32_t)value);
        }
        if (!sqx_huff_build(&state->pre)) return false;
        if (!sqx_read_lengths_pre(bits, &state->pre, &state->table_a)) return false;
        if (!sqx_read_lengths_pre(bits, &state->pre, &state->table_b)) return false;
        if (!sqx_read_lengths_delta(bits, &state->table_len)) return false;
        if (!sqx_read_lengths_delta(bits, &state->table_dist)) return false;

        return true;
    }

    *threshold_out = 0;
    if (!sqx_get(bits, 1, &bit)) return false;
    if (bit == 0) {
        for (i = 0; i < SQX_NPRE; i++) {
            if (!sqx_get(bits, 4, &value)) return false;
            sqx_huff_set_length(&state->pre, i, (int32_t)value);
        }
        if (!sqx_huff_build(&state->pre)) return false;
        if (!sqx_read_lengths_pre(bits, &state->pre, &state->table_a)) return false;
        if (!sqx_read_lengths_pre(bits, &state->pre, &state->table_len)) return false;
        if (!sqx_read_lengths_pre(bits, &state->pre, &state->table_dist)) return false;

        return true;
    }

    if (!sqx_read_lengths_delta(bits, &state->table_a)) return false;
    if (!sqx_read_lengths_delta(bits, &state->table_len)) return false;
    if (!sqx_read_lengths_delta(bits, &state->table_dist)) return false;

    return true;
}

static void sqx_state_release(sqx_state *state)
{
    if (state->window) xx_mem_free(state->window);
    state->window = 0;
    sqx_huff_release(&state->pre);
    sqx_huff_release(&state->table_a);
    sqx_huff_release(&state->table_b);
    sqx_huff_release(&state->table_len);
    sqx_huff_release(&state->table_dist);
}

/* One 4 MiB window serves every member of the archive; only the cursor and
 * the dictionary size move, exactly as in the reference. */
static bool sqx_state_init(sqx_state *state)
{
    xx_rt_memset(state, 0, sizeof(*state));
    state->window = (uint8_t *)xx_mem_alloc(SQX_MAX_DICT);
    if (!state->window) return false;
    xx_rt_memset(state->window, 0, SQX_MAX_DICT);
    state->dict_size = SQX_MAX_DICT;

    if (!sqx_huff_init(&state->pre, SQX_NPRE, SQX_MAXLEN_PRE)) {
        sqx_state_release(state);
        return false;
    }
    if (!sqx_huff_init(&state->table_a, SQX_NMAIN, SQX_MAXLEN) || !sqx_huff_init(&state->table_b, SQX_NMAIN, SQX_MAXLEN) ||
        !sqx_huff_init(&state->table_len, SQX_NLEN, SQX_MAXLEN) || !sqx_huff_init(&state->table_dist, SQX_NDIST, SQX_MAXLEN)) {
        sqx_state_release(state);
        return false;
    }

    return true;
}

static bool sqx_unpack_member(sqx_state *state, const uint8_t *packed, size_t packed_size, int64_t unpacked_size, uint16_t flags, uint8_t filter,
                              uint8_t *out, size_t out_capacity)
{
    const int32_t *dist_base;
    const int32_t *dist_extra;
    int32_t dist_count;
    int32_t dict_size;
    int32_t dict_kb;
    int64_t output_cap;
    int64_t remaining;
    int32_t count;
    int32_t mode;
    int32_t threshold;
    int32_t previous;
    sqx_bits bits;
    uint8_t *scratch;
    size_t padded;
    bool ok;

    dict_size = (int32_t)((uint32_t)1 << ((((int32_t)flags >> 8) & 0xf) + 15));
    if ((dict_size > SQX_MAX_DICT) || (dict_size <= 0)) dict_size = SQX_MAX_DICT;
    state->dict_size = dict_size;
    state->out = out;
    state->out_capacity = out_capacity;
    state->produced = 0;

    /* The reference applies the non-solid reset BEFORE it gives up on the
     * filter, so the side effect happens either way. */
    if (!(flags & 4)) {
        state->last_length = 0;
        state->last_distance = 0;
        state->rep_index = 0;
        state->rep[0] = 0;
        state->rep[1] = 0;
        state->rep[2] = 0;
        state->rep[3] = 0;
        state->position = 0;
    }
    /* No clamp here on purpose: when a solid member inherits a cursor past
     * the new dictionary size the reference writes one byte at that position
     * -- the window is a full 4 MiB -- and only then wraps to zero. */
    if ((state->position < 0) || (state->position >= SQX_MAX_DICT)) return false;

    /* The "b0" preprocessor filter is not implemented. */
    if (filter) return false;

    dist_base = sqx_d48base;
    dist_extra = sqx_d48extra;
    dist_count = 49;
    dict_kb = dict_size >> 10;
    if (dict_kb == 0x800) {
        dist_base = sqx_d50base;
        dist_extra = sqx_d50extra;
        dist_count = 51;
    } else if (dict_kb == 0x1000) {
        dist_base = sqx_d52base;
        dist_extra = sqx_d52extra;
        dist_count = 53;
    }

    padded = ((packed_size + 3) & ~(size_t)3) + 16;
    scratch = (uint8_t *)xx_mem_alloc(padded);
    if (!scratch) return false;
    xx_rt_memset(scratch, 0, padded);
    if (packed_size) xx_rt_memcpy(scratch, packed, packed_size);
    bits.words = scratch;
    bits.word_count = (int64_t)(padded / 4);
    bits.bit_offset = 0;

    output_cap = unpacked_size + SQX_OVERSHOOT;
    remaining = unpacked_size;
    count = 0;
    mode = 0;
    threshold = 0;
    previous = 0;
    ok = true;

    while (remaining >= 1) {
        int32_t symbol;
        uint32_t extra;

        if (state->produced > output_cap) {
            ok = false;
            break;
        }

        if (count == 0) {
            if (!sqx_read_block_header(state, &bits, &count, &mode, &threshold)) {
                ok = false;
                break;
            }
            previous = threshold;
        }

        symbol = 0;
        if (mode == 0) {
            if (!sqx_huff_decode(&state->table_a, &bits, &symbol)) {
                ok = false;
                break;
            }
        } else if (previous < (threshold + 1)) {
            if (!sqx_huff_decode(&state->table_a, &bits, &symbol)) {
                ok = false;
                break;
            }
            previous = symbol;
        } else {
            if (!sqx_huff_decode(&state->table_b, &bits, &symbol)) {
                ok = false;
                break;
            }
            previous = symbol;
        }
        count--;

        extra = 0;
        if (symbol < 0x100) {
            sqx_put(state, (uint8_t)symbol);
            remaining -= 1;
        } else if (symbol == 0x100) {
            /* A repeat of the previous match before any match exists would
             * spin forever; the reference stream never does it. */
            if (state->last_length <= 0) {
                ok = false;
                break;
            }
            sqx_copy(state, state->last_length, state->last_distance);
            remaining -= state->last_length;
        } else if (symbol < 0x105) {
            int32_t length_symbol = 0;
            state->last_distance = state->rep[(state->rep_index - (uint32_t)(symbol - 0x100)) & 3];
            if (!sqx_huff_decode(&state->table_len, &bits, &length_symbol)) {
                ok = false;
                break;
            }
            if (length_symbol == 0x18) {
                if (!sqx_get(&bits, 14, &extra)) {
                    ok = false;
                    break;
                }
                state->last_length = (int64_t)extra + 0x101;
            } else {
                int64_t length;
                if ((length_symbol < 0) || (length_symbol >= 26)) {
                    ok = false;
                    break;
                }
                length = sqx_lenbase[length_symbol];
                if (sqx_lenextra[length_symbol]) {
                    if (!sqx_get(&bits, sqx_lenextra[length_symbol], &extra)) {
                        ok = false;
                        break;
                    }
                    length += extra;
                }
                length += 2;
                /* The two bumps below look like an off-by-one but are
                 * load-bearing: long distances encode one more length. */
                if (state->last_distance > 0x3fff) length += 1;
                if (state->last_distance > 0x3ffff) length += 1;
                state->last_length = length;
            }
            remaining -= state->last_length;
            sqx_copy(state, state->last_length, state->last_distance);
        } else if (symbol < 0x10d) {
            int32_t i = symbol - 0x105;
            int32_t distance = sqx_l2base[i];
            if (sqx_l2extra[i]) {
                if (!sqx_get(&bits, sqx_l2extra[i], &extra)) {
                    ok = false;
                    break;
                }
                distance += (int32_t)extra;
            }
            remaining -= 2;
            /* Deliberate: the short-match classes do NOT update last_length /
             * last_distance, so symbol 0x100 repeats the last long match. */
            sqx_copy(state, 2, distance + 1);
        } else if (symbol < 0x11c) {
            int32_t i = symbol - 0x10d;
            int32_t distance = sqx_l3base[i];
            if (sqx_l3extra[i]) {
                if (!sqx_get(&bits, sqx_l3extra[i], &extra)) {
                    ok = false;
                    break;
                }
                distance += (int32_t)extra;
            }
            remaining -= 3;
            sqx_copy(state, 3, distance + 1);
        } else {
            int32_t i = symbol - 0x11c;
            int32_t dist_symbol = 0;
            int64_t length = 0;
            int64_t distance;
            if (symbol == 0x134) {
                if (!sqx_get(&bits, 14, &extra)) {
                    ok = false;
                    break;
                }
                length = (int64_t)extra + 0x101;
            } else {
                if ((i < 0) || (i >= 26)) {
                    ok = false;
                    break;
                }
                length = sqx_lenbase[i];
                if (sqx_lenextra[i]) {
                    if (!sqx_get(&bits, sqx_lenextra[i], &extra)) {
                        ok = false;
                        break;
                    }
                    length += extra;
                }
                length += 4;
            }
            if (!sqx_huff_decode(&state->table_dist, &bits, &dist_symbol)) {
                ok = false;
                break;
            }
            if ((dist_symbol < 0) || (dist_symbol >= dist_count)) {
                ok = false;
                break;
            }
            distance = dist_base[dist_symbol];
            if (dist_extra[dist_symbol]) {
                if (!sqx_get(&bits, dist_extra[dist_symbol], &extra)) {
                    ok = false;
                    break;
                }
                distance += extra;
            }
            distance += 1;
            if ((symbol != 0x134) && (distance > 0x3ffff)) length += 1;
            if (distance > 0x7fffffff) {
                ok = false;
                break;
            }
            remaining -= length;
            /* Deliberate: this class does not update last_length /
             * last_distance either. */
            sqx_copy(state, length, (int32_t)distance);
        }
    }

    xx_mem_free(scratch);

    return ok;
}

XXFC_API bool xx_sqx_decode_memory(const uint8_t *input, size_t input_size, const xx_sqx_member *members, size_t member_count, size_t target_index,
                                   uint8_t *output, size_t output_size, size_t *written)
{
    sqx_state state;
    size_t i;
    int64_t replayed;
    uint64_t target_size;
    bool result;

    if (written) *written = 0;
    if (!members || !output || !written) return false;
    if ((member_count == 0) || (target_index >= member_count)) return false;
    if (!input) return false;

    target_size = members[target_index].unpacked_size;
    if (target_size > (uint64_t)XX_SQX_MAX_UNCOMPRESSED_SIZE) return false;
    if (target_size > (uint64_t)output_size) return false;

    if (!sqx_state_init(&state)) return false;

    replayed = 0;
    result = true;
    for (i = 0; i <= target_index; i++) {
        const xx_sqx_member *member = &members[i];
        bool is_target = (i == target_index);
        bool ok;

        if (member->unpacked_size > (uint64_t)XX_SQX_MAX_UNCOMPRESSED_SIZE) {
            result = false;
            break;
        }
        if ((member->data_offset > (uint64_t)input_size) || (member->packed_size > ((uint64_t)input_size - member->data_offset))) {
            result = false;
            break;
        }
        /* Methods 5 and up use a different coder entirely. */
        if ((member->method < 1) || (member->method > 4)) {
            result = false;
            break;
        }

        replayed += (int64_t)member->unpacked_size;
        if (replayed > ((int64_t)XX_SQX_MAX_UNCOMPRESSED_SIZE * 4)) {
            result = false;
            break;
        }

        /* A failing member is not fatal to the replay: the reference keeps
         * the state as the failure left it and moves on.  Only the target
         * member's own result has to be complete. */
        ok = sqx_unpack_member(&state, input + member->data_offset, (size_t)member->packed_size, (int64_t)member->unpacked_size, member->flags,
                               member->filter, is_target ? output : 0, is_target ? (size_t)target_size : 0);
        if (is_target) {
            if (!ok || (state.produced < (int64_t)target_size)) {
                result = false;
                break;
            }
        }
    }

    sqx_state_release(&state);
    if (!result) return false;

    *written = (size_t)target_size;

    return true;
}
