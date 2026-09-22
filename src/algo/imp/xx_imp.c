/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IMP (Technelysium) member codecs.  Ported from the XArchive reference
 * decoder Algos/ximpdecoder.cpp.  The algorithm, the tables and every
 * deliberate quirk of the original are kept as they are there; the places
 * where the original looks wrong but is load-bearing are commented.
 */
#include "xxfclib/algo/imp/xx_imp.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define IMP_EMPTY (-0x120)
#define IMP_SLACK 0x200
#define IMP_LITERAL_SYMBOLS 0x120
#define IMP_CL_SYMBOLS 0x15
#define IMP_BWT_SYMBOLS 0x102
#define IMP_MAX_MULTIPLIER 0x1000000
#define IMP_MAX_WINDOW 0x80000
#define IMP_MAX_DICT 0x20000000

#define IMP_MAX_TABLE 512            /* 1 << 9 */
#define IMP_MAX_TREE (IMP_LITERAL_SYMBOLS * 2)

static const int32_t imp_dist_sizes[3] = {0x2a, 0x0e, 0x1c};
static const int32_t imp_dist_offsets[3] = {0x120, 0x14a, 0x158};

static const uint32_t imp_len_base[20] = {11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227};
static const uint8_t imp_len_extra[20] = {1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5};
static const uint32_t imp_len_mask[20] = {1, 1, 1, 1, 3, 3, 3, 3, 7, 7, 7, 7, 15, 15, 15, 15, 31, 31, 31, 31};

static const uint32_t imp_dist_base[36] = {5,     7,     9,     13,    17,    25,    33,     49,     65,     97,     129,    193,
                                           257,   385,   513,   769,   1025,  1537,  2049,   3073,   4097,   6145,   8193,   12289,
                                           16385, 24577, 32769, 49153, 65537, 98305, 131073, 196609, 262145, 393217, 524289, 786433};
static const uint8_t imp_dist_extra[36] = {1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9,
                                           10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18};
static const uint32_t imp_dist_mask[36] = {1,     1,     3,     3,     7,     7,     15,     15,     31,     31,     63,     63,
                                           127,   127,   255,   255,   511,   511,   1023,   1023,   2047,   2047,   4095,   4095,
                                           8191,  8191,  16383, 16383, 32767, 32767, 65535,  65535,  131071, 131071, 262143, 262143};

static const uint32_t imp_windows[3][8] = {{0x8000, 0x10000, 0x12000, 0x18000, 0x1e000, 0x24000, 0x2a000, 0x30000},
                                           {0x8000, 0x10000, 0x12000, 0x18000, 0x1e000, 0x24000, 0x2a000, 0x30000},
                                           {0x8000, 0x20000, 0x30000, 0x40000, 0x50000, 0x60000, 0x70000, 0x80000}};

/* ------------------------------------------------------------ bit reader -- */
/* LSB first over a 32-bit window, refilled to 25 bits at a time, reading
 * zeros past the end of the buffer. */
typedef struct imp_bits {
    const uint8_t *data;
    int64_t size;
    int64_t position;
    uint32_t buffer;
    int32_t count;
    bool error;
} imp_bits;

static void imp_bits_init(imp_bits *r, const uint8_t *data, int64_t size, int64_t position)
{
    r->data = data;
    r->size = size;
    r->position = position;
    r->buffer = 0;
    r->count = 0;
    r->error = false;
    if ((r->position < 0) || (r->position > r->size)) {
        r->position = r->size;
        r->error = true;
    }
}

static uint32_t imp_peek(imp_bits *r, int32_t bits)
{
    if (r->count < 0) {
        r->error = true;
        return 0;
    }
    if (r->count < bits) {
        while (r->count < 25) {
            uint32_t byte = (r->position < r->size) ? (uint32_t)r->data[r->position] : 0u;
            ++r->position;
            r->buffer |= byte << r->count;
            r->count += 8;
        }
    }

    return r->buffer;
}

static void imp_drop(imp_bits *r, int32_t bits)
{
    r->count -= bits;
    if (bits >= 32) r->buffer = 0;
    else r->buffer >>= bits;
    if (r->count < 0) r->error = true;
}

static uint32_t imp_get(imp_bits *r, int32_t bits)
{
    uint32_t value;
    if (bits <= 0) return 0;
    value = imp_peek(r, bits) & ((bits >= 32) ? 0xffffffffu : ((1u << bits) - 1u));
    imp_drop(r, bits);

    return value;
}

static void imp_align(imp_bits *r)
{
    imp_drop(r, r->count & 7);
}

/* the offset of the next unconsumed byte (the stored-block rewind rule) */
static int64_t imp_byte_position(const imp_bits *r)
{
    return r->position - (int64_t)(r->count >> 3);
}

/* --------------------------------------------------------------- huffman -- */
typedef struct imp_huff {
    int32_t table[IMP_MAX_TABLE];
    int32_t tree[IMP_MAX_TREE];
    int32_t tree_size;
    int32_t bits;
    int32_t count;
} imp_huff;

static void imp_make_codes(const uint8_t *lengths, int32_t count, uint32_t *codes)
{
    int32_t counts[17];
    int32_t next[17];
    int32_t i;
    int32_t bit;
    uint32_t code;

    for (i = 0; i < 17; ++i) {
        counts[i] = 0;
        next[i] = 0;
    }
    /* Lengths run up to 16, so masking them to four bits would silently turn
     * a 16-bit code into an absent one. */
    for (i = 0; i < count; ++i) {
        if (lengths[i] > 16) continue;
        counts[lengths[i]]++;
    }
    counts[0] = 0;
    code = 0;
    for (bit = 1; bit < 17; ++bit) {
        code = (code + (uint32_t)counts[bit - 1]) * 2u;
        next[bit] = (int32_t)code;
    }
    for (i = 0; i < count; ++i) {
        int32_t length = (lengths[i] > 16) ? 0 : (int32_t)lengths[i];
        uint32_t current;
        uint32_t reversed;
        int32_t j;
        codes[i] = 0;
        if (!length) continue;
        current = (uint32_t)next[length];
        next[length] = (int32_t)(current + 1u);
        reversed = 0;
        for (j = 0; j < length; ++j) {
            reversed = reversed * 2u + (current & 1u);
            current >>= 1;
        }
        codes[i] = reversed;
    }
}

/* The reference layout: a direct lookup table of `bits` bits plus an
 * overflow tree.  The tree is always built (the reference never asks for a
 * table-only build). */
static bool imp_huff_make(imp_huff *huff, const uint8_t *lengths, int32_t bits, int32_t count)
{
    uint32_t codes[IMP_LITERAL_SYMBOLS];
    int32_t size;
    int32_t node;
    int32_t symbol;
    int32_t i;

    if ((count <= 0) || (count > IMP_LITERAL_SYMBOLS)) return false;
    size = 1 << bits;
    if (size > IMP_MAX_TABLE) return false;
    huff->bits = bits;
    huff->count = count;
    huff->tree_size = count * 2;
    for (i = 0; i < size; ++i) huff->table[i] = IMP_EMPTY;
    for (i = 0; i < huff->tree_size; ++i) huff->tree[i] = IMP_EMPTY;

    imp_make_codes(lengths, count, codes);

    node = 1;
    for (symbol = 0; symbol < count; ++symbol) {
        int32_t length = (int32_t)lengths[symbol];
        int32_t span;
        uint32_t code;
        if ((length == 0) || (length > 16)) continue;
        span = 1 << length;
        code = codes[symbol];
        if (size < span) {
            int32_t remaining = length - bits - 1;
            int32_t index = (int32_t)(code & (uint32_t)(size - 1));
            uint32_t rest = code >> bits;
            int32_t current = 0;
            int32_t next = 0;
            if (huff->table[index] == IMP_EMPTY) {
                huff->table[index] = -node;
                current = node;
                next = node + 1;
            } else {
                int32_t guard = 0;
                current = -huff->table[index];
                next = node;
                if ((current < 1) || (current >= count)) return false;
                while (huff->tree[current * 2 + (int32_t)(rest & 1u)] >= 0) {
                    current = huff->tree[current * 2 + (int32_t)(rest & 1u)];
                    rest >>= 1;
                    --remaining;
                    if ((current < 1) || (current >= count)) return false;
                    if (++guard > count) return false;
                }
            }
            for (;;) {
                node = next;
                if (remaining == 0) break;
                if (remaining < 0) return false;
                --remaining;
                if (node >= count) return false;
                if ((current < 1) || (current >= count)) return false;
                huff->tree[current * 2 + (int32_t)(rest & 1u)] = node;
                rest >>= 1;
                current = node;
                next = node + 1;
            }
            if ((current < 1) || (current >= count)) return false;
            huff->tree[current * 2 + (int32_t)(rest & 1u)] = -symbol;
        } else {
            int32_t c;
            for (c = (int32_t)code; c < size; c += span) huff->table[c] = symbol;
        }
    }

    return true;
}

/* -1 on a bad code */
static int32_t imp_decode_symbol(imp_bits *r, const imp_huff *huff)
{
    int32_t symbol;

    symbol = huff->table[(int32_t)(imp_peek(r, huff->bits) & (uint32_t)((1 << huff->bits) - 1))];
    if (symbol < 0) {
        uint32_t rest;
        int32_t guard = 0;
        if (symbol == IMP_EMPTY) return -1;
        rest = imp_peek(r, 16) >> huff->bits;
        symbol = -symbol;
        for (;;) {
            if ((symbol < 1) || ((symbol * 2 + 1) >= huff->tree_size)) return -1;
            symbol = huff->tree[symbol * 2 + (int32_t)(rest & 1u)];
            rest >>= 1;
            if (symbol <= 0) break;
            if (++guard > huff->count) return -1;
        }
        if (symbol == IMP_EMPTY) return -1;
        symbol = -symbol;
    }
    if ((symbol < 0) || (symbol >= huff->count)) return -1;

    return symbol;
}

/* ---------------------------------------------------- code length arrays -- */
static void imp_default_lengths(uint8_t *out, int64_t window)
{
    int32_t i;

    for (i = 0x00; i < 0xf4; ++i) out[i] = 8;
    for (i = 0xf4; i < 0x105; ++i) out[i] = 9;
    out[0x100] = 0;
    for (i = 0x105; i < 0x10a; ++i) out[i] = 10;
    for (i = 0x10a; i < 0x120; ++i) out[i] = 11;
    if (window < 0x2001) {
        for (i = 0x120; i < 0x124; ++i) out[i] = 4;
        for (i = 0x124; i < 0x13c; ++i) out[i] = 5;
        for (i = 0x13c; i < 0x14a; ++i) out[i] = 0;
    } else {
        for (i = 0x120; i < 0x136; ++i) out[i] = 5;
        for (i = 0x136; i < 0x14a; ++i) out[i] = 6;
    }
    for (i = 0x14a; i < 0x158; ++i) out[i] = 4;
    out[0x14a] = 3;
    out[0x14b] = 3;
}

static bool imp_read_lengths(imp_bits *r, uint8_t *out, int64_t out_size, int32_t stride, int32_t tables, int64_t window)
{
    uint8_t code_lengths[IMP_CL_SYMBOLS];
    imp_huff *huff;
    int32_t position;
    int32_t multiplier;
    int64_t end;
    int32_t previous;
    int32_t repeat_multiplier;
    int32_t zero_multiplier;
    int64_t p;
    bool ok;

    if (tables == 0) {
        if (out_size < 0x158) return false;
        imp_default_lengths(out, window);
        return true;
    }

    xx_rt_memset(code_lengths, 0, sizeof(code_lengths));
    position = 0;
    multiplier = 1;
    while (position < IMP_CL_SYMBOLS) {
        int32_t value = (int32_t)imp_get(r, 5);
        if (r->error) return false;
        if (value < 2) {
            position += multiplier << value;
            /* The reference lets the multiplier double without bound;
             * saturating it is equivalent, because every count it feeds is
             * clamped to a range far below the saturation value. */
            if (multiplier < IMP_MAX_MULTIPLIER) multiplier *= 2;
        } else {
            if (value > 0x11) return false;
            code_lengths[position] = (uint8_t)(value - 1);
            ++position;
            multiplier = 1;
        }
    }

    huff = (imp_huff *)xx_mem_alloc(sizeof(imp_huff));
    if (!huff) return false;
    if (!imp_huff_make(huff, code_lengths, 6, IMP_CL_SYMBOLS)) {
        xx_mem_free(huff);
        return false;
    }

    end = (int64_t)stride * tables;
    if (end > out_size) {
        xx_mem_free(huff);
        return false;
    }

    previous = 0;
    repeat_multiplier = 1;
    zero_multiplier = 1;
    p = 0;
    ok = true;
    while (p < end) {
        int32_t symbol = imp_decode_symbol(r, huff);
        if (symbol < 0) {
            ok = false;
            break;
        }
        imp_drop(r, (int32_t)code_lengths[symbol]);
        if (r->error) {
            ok = false;
            break;
        }

        if (symbol < 0x13) {
            if (symbol < 0x11) {
                if ((p >= stride) && (out[p - stride] != 0)) previous = out[p - stride];
                previous = symbol + previous;
                /* Deliberate: the delta wraps modulo 0x11, not modulo 16. */
                if (previous > 0x10) previous -= 0x11;
                out[p] = (uint8_t)previous;
                ++p;
                repeat_multiplier = 1;
                zero_multiplier = 1;
            } else {
                int64_t run = (int64_t)zero_multiplier << (symbol - 0x11);
                while (run && (p < end)) {
                    --run;
                    out[p] = 0;
                    ++p;
                }
                if (zero_multiplier < IMP_MAX_MULTIPLIER) zero_multiplier *= 2;
                repeat_multiplier = 1;
            }
        } else {
            int64_t run = (int64_t)repeat_multiplier << (symbol - 0x13);
            while (run && (p < end)) {
                --run;
                if ((p >= stride) && (out[p - stride] != 0)) previous = out[p - stride];
                out[p] = (uint8_t)previous;
                ++p;
            }
            if (repeat_multiplier < IMP_MAX_MULTIPLIER) repeat_multiplier *= 2;
            zero_multiplier = 1;
        }
    }

    xx_mem_free(huff);

    return ok;
}

/* -------------------------------------------------------- delta filter ---- */
typedef struct imp_record {
    int64_t position;
    int32_t distance;
} imp_record;

typedef struct imp_records {
    imp_record *items;
    int32_t count;
    int32_t capacity;
} imp_records;

static void imp_records_clear(imp_records *recs)
{
    recs->count = 0;
}

static void imp_records_release(imp_records *recs)
{
    if (recs->items) xx_mem_free(recs->items);
    recs->items = 0;
    recs->count = 0;
    recs->capacity = 0;
}

static bool imp_records_append(imp_records *recs, int64_t position, int32_t distance)
{
    if (recs->count == recs->capacity) {
        int32_t capacity = recs->capacity ? (recs->capacity * 2) : 64;
        imp_record *items;
        if (capacity > 0x01000000) return false;
        items = (imp_record *)xx_mem_alloc(sizeof(imp_record) * (size_t)capacity);
        if (!items) return false;
        if (recs->count) xx_rt_memcpy(items, recs->items, sizeof(imp_record) * (size_t)recs->count);
        if (recs->items) xx_mem_free(recs->items);
        recs->items = items;
        recs->capacity = capacity;
    }
    recs->items[recs->count].position = position;
    recs->items[recs->count].distance = distance;
    ++recs->count;

    return true;
}

/* The reference behaviour.  A source index below zero cannot happen for a
 * well formed stream; the reference would read outside its buffer, so it
 * reads as zero. */
static void imp_delta_apply(const imp_records *recs, uint8_t *buffer, int64_t buffer_size, int64_t total)
{
    int32_t i;

    for (i = 0; i < recs->count; ++i) {
        int32_t distance = recs->items[i].distance;
        int64_t from;
        int64_t to;
        int64_t p;
        if (distance == 0) continue;
        from = recs->items[i].position;
        to = ((i + 1) < recs->count) ? recs->items[i + 1].position : total;
        if (to > buffer_size) to = buffer_size;
        for (p = from; p < to; ++p) {
            uint8_t addend;
            if (p < 0) continue;
            addend = ((p - distance) >= 0) ? buffer[p - distance] : 0;
            buffer[p] = (uint8_t)(buffer[p] + addend);
        }
    }
}

/* The reference behaviour - undo the filter over the window that will be kept */
static void imp_delta_unapply(const imp_records *recs, uint8_t *buffer, int64_t buffer_size, int64_t dict_length, int64_t keep)
{
    int64_t low;
    int32_t i;

    low = dict_length - keep;
    if (low < 0) low = 0;
    for (i = recs->count - 1; i >= 0; --i) {
        int32_t distance = recs->items[i].distance;
        int64_t from;
        int64_t to;
        int64_t p;
        if (distance == 0) continue;
        from = recs->items[i].position;
        if (from < low) from = low;
        to = ((i + 1) < recs->count) ? recs->items[i + 1].position : dict_length;
        if (to > buffer_size) to = buffer_size;
        for (p = to - 1; p >= from; --p) {
            uint8_t addend;
            if (p < 0) break;
            addend = ((p - distance) >= 0) ? buffer[p - distance] : 0;
            buffer[p] = (uint8_t)(buffer[p] - addend);
        }
        if (recs->items[i].position <= low) return;
    }
}

/* ----------------------------------------------------------- LZ77 blocks -- */
typedef struct imp_lz_tables {
    imp_huff literal;
    imp_huff dist[3];
} imp_lz_tables;

/* Decodes into buffer[start .. start + block_length).  *produced gets the
 * number of bytes actually produced. */
static bool imp_lz_decode(imp_bits *r, uint8_t *buffer, int64_t buffer_size, int64_t start, int64_t block_length, imp_records *recs,
                          bool apply_filter, int64_t *produced)
{
    imp_lz_tables *tables;
    uint8_t *lengths;
    int64_t lengths_size;
    int64_t alloc;
    int32_t mode;
    int32_t big;
    int32_t stride;
    int32_t table_count;
    int64_t out;
    int64_t end;
    int64_t pointer;
    int64_t recent0;
    int64_t recent1;
    int32_t symbol;
    bool dist_ready[3];
    bool literal_ready;
    bool ok;

    if (apply_filter) imp_records_clear(recs);
    if ((start < 0) || (block_length < 0) || ((start + block_length) > buffer_size)) return false;

    mode = (int32_t)imp_get(r, 2);
    if (mode == 0) return false;
    big = (mode > 2) ? 1 : 0;
    stride = big ? 0x174 : 0x158;

    out = start;
    end = start + block_length;
    table_count = (int32_t)imp_get(r, 6);
    if (r->error) return false;

    alloc = table_count ? ((int64_t)stride * table_count) : 0x158;
    if (alloc > 0x00400000) return false;
    lengths_size = alloc + stride + IMP_SLACK;
    lengths = (uint8_t *)xx_mem_alloc((size_t)lengths_size);
    if (!lengths) return false;
    xx_rt_memset(lengths, 0, (size_t)lengths_size);
    if (!imp_read_lengths(r, lengths, lengths_size, stride, table_count, start + block_length)) {
        xx_mem_free(lengths);
        return false;
    }

    tables = (imp_lz_tables *)xx_mem_alloc(sizeof(imp_lz_tables));
    if (!tables) {
        xx_mem_free(lengths);
        return false;
    }
    xx_rt_memset(tables, 0, sizeof(*tables));

    pointer = -stride;
    recent0 = 0;
    recent1 = 0;
    symbol = 0x100;
    if (table_count == 0) table_count = 1;
    dist_ready[0] = false;
    dist_ready[1] = false;
    dist_ready[2] = false;
    literal_ready = false;
    ok = true;

    while (out < end) {
        int64_t length_index;
        int64_t length;
        int32_t which;
        int32_t dist_symbol;
        int64_t dist_index;
        int64_t distance;

        if (symbol == 0x100) {
            int64_t row;
            int32_t i;
            if (table_count == 0) break;
            --table_count;
            pointer += stride;
            row = pointer;
            if ((row < 0) || ((row + IMP_LITERAL_SYMBOLS) > lengths_size)) {
                ok = false;
                break;
            }
            if (!imp_huff_make(&tables->literal, lengths + row, 9, IMP_LITERAL_SYMBOLS)) {
                ok = false;
                break;
            }
            literal_ready = true;
            pointer += IMP_LITERAL_SYMBOLS;
            for (i = 0; i < mode; ++i) {
                int32_t n = imp_dist_sizes[i];
                if ((pointer < 0) || ((pointer + n) > lengths_size)) {
                    ok = false;
                    break;
                }
                if (!imp_huff_make(&tables->dist[i], lengths + pointer, 7, n)) {
                    ok = false;
                    break;
                }
                dist_ready[i] = true;
                pointer += n;
            }
            if (!ok) break;
            pointer -= stride;
        }
        if (!literal_ready) {
            ok = false;
            break;
        }

        symbol = imp_decode_symbol(r, &tables->literal);
        if (symbol < 0) {
            ok = false;
            break;
        }
        /* Mode 1 leaves `pointer` 0x0e bytes below the row - a real
         * off-by-one in the original, whose stride is hard coded 0x158 while
         * only 0x14a bytes of it are used.  Do not "fix" it: the index can
         * legitimately be negative and the original then drops zero bits. */
        length_index = pointer + symbol;
        if (length_index >= lengths_size) {
            ok = false;
            break;
        }
        imp_drop(r, (length_index >= 0) ? (int32_t)lengths[length_index] : 0);
        if (r->error) {
            ok = false;
            break;
        }

        if (symbol < 0x100) {
            buffer[out] = (uint8_t)symbol;
            ++out;
            continue;
        }
        if (symbol == 0x100) continue;

        length = 0;
        if (symbol < 0x10a) {
            length = symbol - 0xff;
        } else if (symbol < 0x11e) {
            int32_t k = symbol - 0x10a;
            uint32_t value = imp_peek(r, 5);
            imp_drop(r, (int32_t)imp_len_extra[k]);
            length = (int64_t)((value & imp_len_mask[k]) + imp_len_base[k]);
        } else if (symbol == 0x11e) {
            length = (int64_t)(imp_peek(r, 15) & 0x7fffu) + 0x103;
            imp_drop(r, 15);
        } else {
            /* 0x11f: a delta post-filter record, not a match */
            int32_t distance3 = (int32_t)imp_get(r, 3);
            if (!imp_records_append(recs, out, distance3)) {
                ok = false;
                break;
            }
            continue;
        }
        if (r->error) {
            ok = false;
            break;
        }

        which = ((length == 2) ? 1 : 0) + ((big & ((length == 3) ? 1 : 0)) * 2);
        if ((which < 0) || (which > 2) || !dist_ready[which]) {
            ok = false;
            break;
        }
        dist_symbol = imp_decode_symbol(r, &tables->dist[which]);
        if (dist_symbol < 0) {
            ok = false;
            break;
        }
        dist_index = pointer + dist_symbol + imp_dist_offsets[which];
        if (dist_index >= lengths_size) {
            ok = false;
            break;
        }
        imp_drop(r, (dist_index >= 0) ? (int32_t)lengths[dist_index] : 0);
        if (r->error) {
            ok = false;
            break;
        }

        distance = 0;
        if (dist_symbol == 0) {
            distance = recent0;
        } else if (dist_symbol == 1) {
            distance = recent1;
            recent1 = recent0;
            recent0 = distance;
        } else if (dist_symbol < 6) {
            distance = dist_symbol - 1;
        } else {
            int32_t k = dist_symbol - 6;
            uint32_t value;
            if (k >= 36) {
                ok = false;
                break;
            }
            value = imp_peek(r, 18);
            imp_drop(r, (int32_t)imp_dist_extra[k]);
            distance = (int64_t)((value & imp_dist_mask[k]) + imp_dist_base[k]);
            recent1 = recent0;
            recent0 = distance;
        }
        if (r->error) {
            ok = false;
            break;
        }

        /* Deliberate: when the distance reaches past the start of the
         * buffer the reference emits NOTHING for this match rather than
         * failing.  A short block is reported through *produced and the
         * caller treats it as the last one. */
        if ((distance <= out) && (distance >= 0)) {
            while (length && (out < end)) {
                --length;
                buffer[out] = buffer[out - distance];
                ++out;
            }
        }
    }

    xx_mem_free(tables);
    xx_mem_free(lengths);
    if (!ok) return false;

    if (apply_filter) imp_delta_apply(recs, buffer, buffer_size, start + block_length);
    if (produced) *produced = out - start;

    return true;
}

/* ------------------------------------------------------------ BWT method -- */
/* NOTE: this writes block_length + 1 bytes into the buffer, one past the
 * block.  That is why every allocation carries IMP_SLACK. */
static bool imp_bwt_decode(imp_bits *r, uint8_t *buffer, int64_t buffer_size, int64_t start, int64_t block_length)
{
    imp_huff *huffs;
    uint8_t *lengths;
    uint32_t *transform;
    int64_t counts[256];
    int64_t cumulative[257];
    int64_t index[257];
    int32_t mtf[256];
    int32_t selector[8];
    uint32_t add;
    int32_t table_count;
    int32_t i;
    int64_t out;
    int64_t end;
    int32_t group;
    int32_t current;
    int64_t run_multiplier;
    int64_t primary;
    uint32_t pointer;
    bool ok;

    if ((start < 0) || (block_length < 0) || ((start + block_length + 1) > buffer_size)) return false;

    add = imp_get(r, 8);
    table_count = (int32_t)imp_get(r, 3);
    if (r->error || (table_count < 1)) return false;

    huffs = (imp_huff *)xx_mem_alloc(sizeof(imp_huff) * (size_t)table_count);
    lengths = (uint8_t *)xx_mem_alloc((size_t)(IMP_BWT_SYMBOLS + IMP_SLACK) * (size_t)table_count);
    if (!huffs || !lengths) {
        if (huffs) xx_mem_free(huffs);
        if (lengths) xx_mem_free(lengths);
        return false;
    }
    xx_rt_memset(lengths, 0, (size_t)(IMP_BWT_SYMBOLS + IMP_SLACK) * (size_t)table_count);

    ok = true;
    for (i = 0; i < table_count; ++i) {
        uint8_t *row = lengths + (size_t)i * (size_t)(IMP_BWT_SYMBOLS + IMP_SLACK);
        if (!imp_read_lengths(r, row, IMP_BWT_SYMBOLS + IMP_SLACK, IMP_BWT_SYMBOLS, 1, 0)) {
            ok = false;
            break;
        }
        if (!imp_huff_make(&huffs[i], row, 9, IMP_BWT_SYMBOLS)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        xx_mem_free(huffs);
        xx_mem_free(lengths);
        return false;
    }

    imp_align(r);

    for (i = 0; i < 256; ++i) {
        mtf[i] = i;
        counts[i] = 0;
    }
    for (i = 0; i < 8; ++i) selector[i] = i;

    out = start;
    end = start + block_length + 1;
    group = 0x32;
    current = 0;
    run_multiplier = 1;
    primary = 0;

    while (out < end) {
        int32_t symbol;
        if (group == 0x32) {
            uint32_t value = imp_peek(r, table_count);
            int32_t j = 0;
            while (value & 1u) {
                ++j;
                value >>= 1;
                if (j > table_count) break;
            }
            if (j >= table_count) {
                ok = false;
                break;
            }
            imp_drop(r, j + 1);
            current = selector[j];
            if (j) {
                while (j) {
                    selector[j] = selector[j - 1];
                    --j;
                }
                selector[0] = current;
            }
            if ((current < 0) || (current >= table_count)) {
                ok = false;
                break;
            }
            group = 0;
            continue;
        }
        ++group;

        symbol = imp_decode_symbol(r, &huffs[current]);
        if (symbol < 0) {
            ok = false;
            break;
        }
        imp_drop(r, (int32_t)lengths[(size_t)current * (size_t)(IMP_BWT_SYMBOLS + IMP_SLACK) + (size_t)symbol]);
        if (r->error) {
            ok = false;
            break;
        }

        if (symbol < 2) {
            int64_t run = run_multiplier << symbol;
            int32_t byte;
            int64_t k;
            if (end < (out + run)) run = end - out;
            byte = mtf[0];
            counts[byte] += run;
            for (k = 0; k < run; ++k) {
                buffer[out] = (uint8_t)byte;
                ++out;
            }
            if (run_multiplier < (int64_t)IMP_MAX_MULTIPLIER) run_multiplier <<= 1;
        } else if (symbol < 0x101) {
            int32_t k = symbol - 1;
            int32_t byte = mtf[k];
            buffer[out] = (uint8_t)byte;
            ++out;
            while (k) {
                mtf[k] = mtf[k - 1];
                --k;
            }
            mtf[0] = byte;
            counts[byte] += 1;
            run_multiplier = 1;
        } else {
            primary = out - start;
            ++out;
            run_multiplier = 1;
        }
    }

    xx_mem_free(huffs);
    xx_mem_free(lengths);
    if (!ok) return false;

    if ((primary < 0) || (primary > block_length)) return false;
    if (block_length > 0x00ffffff) return false;

    /* inverse BWT */
    cumulative[0] = 1;
    for (i = 0; i < 256; ++i) cumulative[i + 1] = cumulative[i] + counts[i];
    for (i = 0; i < 257; ++i) index[i] = cumulative[i];

    transform = (uint32_t *)xx_mem_alloc(sizeof(uint32_t) * (size_t)(block_length + 1));
    if (!transform) return false;
    xx_rt_memset(transform, 0, sizeof(uint32_t) * (size_t)(block_length + 1));

    {
        int64_t k;
        for (k = 0; k <= block_length; ++k) {
            int32_t byte;
            int64_t slot;
            if (k == primary) continue;
            byte = buffer[start + k];
            slot = index[byte];
            if ((slot < 0) || (slot > 0x00ffffff)) {
                xx_mem_free(transform);
                return false;
            }
            transform[k] = ((uint32_t)byte << 24) | (uint32_t)slot;
            index[byte] = slot + 1;
        }
        transform[primary] = 0;

        pointer = 0;
        for (k = block_length - 1; k >= 0; --k) {
            int32_t slot = (int32_t)(pointer & 0xffffffu);
            if ((slot < 0) || ((int64_t)slot > block_length)) {
                xx_mem_free(transform);
                return false;
            }
            pointer = transform[slot];
            buffer[start + k] = (uint8_t)(((pointer >> 24) + add) & 0xffu);
        }
    }

    xx_mem_free(transform);

    return true;
}

/* ------------------------------------------------- x86 branch converter --- */
/* Converts x86 CALL rel operands back from absolute to relative.  The last
 * `width` bytes are never converted.
 * NOTE: no corpus sample sets the attribute bits that enable this, so it is
 * transcribed from the reference but not empirically validated. */
static void imp_branch_convert(uint8_t *data, int64_t size, int32_t width, int64_t total)
{
    int64_t limit;
    int64_t i;

    limit = size - width;
    i = 0;
    while (i < limit) {
        uint8_t byte = data[i];
        ++i;
        if (byte != 0xe8) continue;
        if (width == 2) {
            uint32_t value;
            uint32_t result;
            if ((i + 2) > size) break;
            value = (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8);
            result = (value - (uint32_t)(i & 0xffff)) & 0xffffu;
            data[i] = (uint8_t)(result & 0xff);
            data[i + 1] = (uint8_t)((result >> 8) & 0xff);
            i += 2;
        } else {
            int32_t value;
            uint32_t result;
            if ((i + 4) > size) break;
            value = (int32_t)((uint32_t)data[i] | ((uint32_t)data[i + 1] << 8) | ((uint32_t)data[i + 2] << 16) | ((uint32_t)data[i + 3] << 24));
            if ((value < 0) && (-i <= (int64_t)value)) result = (uint32_t)(total - value - i - 1);
            else if ((uint32_t)value < (uint32_t)total) result = (uint32_t)(value - i);
            else result = (uint32_t)value;
            data[i] = (uint8_t)(result & 0xff);
            data[i + 1] = (uint8_t)((result >> 8) & 0xff);
            data[i + 2] = (uint8_t)((result >> 16) & 0xff);
            data[i + 3] = (uint8_t)((result >> 24) & 0xff);
            i += 4;
        }
    }
}

/* ---------------------------------------------------------- block driver -- */
typedef struct imp_stream {
    const uint8_t *data;
    int64_t data_size;
    uint8_t *dict;
    int64_t dict_capacity;
    int64_t position;
    int64_t dict_length;
    int64_t block_length;
    int64_t out_base;
    bool first;
    bool eof;
    imp_records recs;
} imp_stream;

static void imp_stream_init(imp_stream *s, const uint8_t *data, int64_t data_size, int64_t offset)
{
    xx_rt_memset(s, 0, sizeof(*s));
    s->data = data;
    s->data_size = data_size;
    s->position = offset;
    s->first = true;
}

static void imp_stream_release(imp_stream *s)
{
    if (s->dict) xx_mem_free(s->dict);
    s->dict = 0;
    s->dict_capacity = 0;
    imp_records_release(&s->recs);
}

/* The dictionary only ever grows, and the new tail is zeroed - the same
 * observable behaviour as the reference's append of zero bytes. */
static bool imp_stream_grow(imp_stream *s, int64_t size)
{
    uint8_t *dict;

    if ((size < 0) || (size > IMP_MAX_DICT)) return false;
    if (s->dict_capacity >= size) return true;
    dict = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!dict) return false;
    xx_rt_memset(dict, 0, (size_t)size);
    if (s->dict_capacity) xx_rt_memcpy(dict, s->dict, (size_t)s->dict_capacity);
    if (s->dict) xx_mem_free(s->dict);
    s->dict = dict;
    s->dict_capacity = size;

    return true;
}

static bool imp_stream_next_block(imp_stream *s)
{
    imp_bits r;
    int32_t method;
    int32_t last;
    int64_t unpacked;
    int64_t packed;

    if (s->eof) return false;
    imp_bits_init(&r, s->data, s->data_size, s->position);
    method = (int32_t)imp_get(&r, 4);
    if (method > 3) return false;
    last = (int32_t)imp_get(&r, 1);
    unpacked = (int64_t)imp_get(&r, 20);
    packed = (int64_t)imp_get(&r, 20);
    if (r.error) return false;

    s->dict_length += s->block_length;
    s->out_base += s->block_length;
    s->block_length = unpacked;

    if ((method == 1) || (method == 3)) {
        int64_t produced = 0;
        if (s->first) {
            imp_drop(&r, 5);
        } else {
            int32_t row = (int32_t)imp_get(&r, 2);
            int32_t column = (int32_t)imp_get(&r, 3);
            int64_t window;
            if (r.error || (row > 2)) return false;
            window = (int64_t)imp_windows[row][column];
            imp_delta_unapply(&s->recs, s->dict, s->dict_capacity, s->dict_length, window);
            if (window < s->dict_length) {
                xx_rt_memmove(s->dict, s->dict + (s->dict_length - window), (size_t)window);
                s->dict_length = window;
            }
        }
        if (!imp_stream_grow(s, s->dict_length + unpacked + IMP_SLACK)) return false;
        if (!imp_lz_decode(&r, s->dict, s->dict_capacity, s->dict_length, unpacked, &s->recs, true, &produced)) return false;
        /* A block that fell short is the last one - the reference trusts the
         * decoder, not the header. */
        if (produced < unpacked) {
            s->block_length = produced;
            last = 1;
        }
    } else {
        if (!imp_stream_grow(s, unpacked + IMP_SLACK)) return false;
        if (!last) {
            int64_t keep = IMP_MAX_WINDOW - unpacked;
            if (keep < 0) keep = 0;
            else if (((s->dict_capacity - IMP_SLACK) - unpacked) < keep) keep = (s->dict_capacity - IMP_SLACK) - unpacked;
            if (keep < 0) keep = 0;
            imp_delta_unapply(&s->recs, s->dict, s->dict_capacity, s->dict_length, keep);
            if (keep < s->dict_length) {
                xx_rt_memmove(s->dict, s->dict + (s->dict_length - keep), (size_t)keep);
                s->dict_length = keep;
            }
        } else {
            s->dict_length = 0;
        }
        if (!imp_stream_grow(s, s->dict_length + unpacked + IMP_SLACK)) return false;
        imp_records_clear(&s->recs);
        if (method == 2) {
            if (!imp_bwt_decode(&r, s->dict, s->dict_capacity, s->dict_length, unpacked)) return false;
        } else {
            int64_t source = imp_byte_position(&r);
            int64_t available;
            if ((source < 0) || (source > s->data_size)) return false;
            available = s->data_size - source;
            if (available > unpacked) available = unpacked;
            if (available > 0) xx_rt_memcpy(s->dict + s->dict_length, s->data + source, (size_t)available);
            if (available < unpacked) xx_rt_memset(s->dict + s->dict_length + available, 0, (size_t)(unpacked - available));
        }
    }

    s->eof = (last != 0);
    s->first = false;
    s->position += packed;
    if ((packed <= 0) && !s->eof) return false; /* a zero-length block would spin */

    return true;
}

static bool imp_stream_read(imp_stream *s, int64_t from, int64_t length, uint8_t *dest, int64_t dest_size)
{
    int64_t position;
    int64_t got;

    if ((from < 0) || (length < 0) || (length > XX_IMP_MAX_UNCOMPRESSED_SIZE)) return false;
    if (length > dest_size) return false;
    position = from;
    got = 0;
    while (got < length) {
        if (position < s->out_base) return false; /* the stream cannot rewind */
        if (position < (s->out_base + s->block_length)) {
            int64_t low = s->dict_length + (position - s->out_base);
            int64_t available = s->block_length - (position - s->out_base);
            int64_t wanted = length - got;
            if (available > wanted) available = wanted;
            if ((low < 0) || ((low + available) > s->dict_capacity)) return false;
            if (available > 0) xx_rt_memcpy(dest + got, s->dict + low, (size_t)available);
            got += available;
            position += available;
        } else {
            if (!imp_stream_next_block(s)) break;
        }
    }

    return got == length;
}

/* ------------------------------------------------------------ public API -- */
XXFC_API bool xx_imp_decode_memory(const uint8_t *input, size_t input_size, uint64_t stream_offset, uint8_t attributes, uint8_t *output,
                                   size_t output_size, size_t *written)
{
    imp_stream stream;
    uint8_t head[11];
    int64_t skip;
    bool ok;

    if (written) *written = 0;
    if (!input || !output || !written) return false;
    if (input_size < 6) return false;
    if (input_size > 0x7fffffff) return false;
    if (output_size > (size_t)XX_IMP_MAX_UNCOMPRESSED_SIZE) return false;
    if (stream_offset > (uint64_t)XX_IMP_MAX_UNCOMPRESSED_SIZE) return false;

    /* the block chain starts just past the stream's own six-byte signature */
    imp_stream_init(&stream, input, (int64_t)input_size, 6);

    ok = imp_stream_read(&stream, (int64_t)stream_offset, 11, head, (int64_t)sizeof(head));
    if (ok) {
        /* the in-stream header: u32 size, u16 name length, u16 version,
         * u16 crc16, u8 attributes - the name follows it */
        skip = (int64_t)((uint32_t)head[4] | ((uint32_t)head[5] << 8));
        ok = imp_stream_read(&stream, (int64_t)stream_offset + 11 + skip, (int64_t)output_size, output, (int64_t)output_size);
    }

    imp_stream_release(&stream);
    if (!ok) return false;

    if (attributes & 0x06) imp_branch_convert(output, (int64_t)output_size, (attributes & 4) ? 4 : 2, (int64_t)output_size);

    *written = output_size;

    return true;
}

XXFC_API bool xx_imp_decode_directory(const uint8_t *directory, size_t directory_size, uint32_t records, uint8_t *output, size_t output_size,
                                      uint32_t *chunk_sizes, size_t chunk_capacity, size_t *chunk_count, size_t *written)
{
    static const uint8_t tag[6] = {'I', 'M', 'P', 'D', 'E', 0};
    int64_t offset;
    int64_t total;
    int64_t size;
    size_t chunks;
    size_t produced_total;

    if (written) *written = 0;
    if (chunk_count) *chunk_count = 0;
    if (!directory || !output || !chunk_sizes || !written) return false;
    if (directory_size > 0x7fffffff) return false;

    offset = 0;
    total = 0;
    size = (int64_t)directory_size;
    chunks = 0;
    produced_total = 0;

    for (;;) {
        imp_bits r;
        int32_t method;
        int64_t unpacked;
        int64_t packed;

        if (((size - offset) < 6) || xx_rt_memcmp(directory + offset, tag, 6)) break;

        imp_bits_init(&r, directory, size, offset + 6);
        method = (int32_t)imp_get(&r, 5);
        if ((method & 0x0f) >= 2) return false;
        unpacked = (int64_t)imp_get(&r, 20);
        if (unpacked > 0x2000) return false;
        packed = (int64_t)imp_get(&r, 20);
        if (r.error) return false;

        if (chunks >= chunk_capacity) return false;
        if ((uint64_t)unpacked > (uint64_t)(output_size - produced_total)) return false;

        if ((method & 0x0f) == 1) {
            imp_records recs;
            int64_t produced = 0;
            bool ok;
            xx_rt_memset(&recs, 0, sizeof(recs));
            imp_drop(&r, 5);
            /* The directory chunks are decoded with the filter records
             * collected but never applied, exactly as in the reference. */
            ok = imp_lz_decode(&r, output + produced_total, unpacked, 0, unpacked, &recs, false, &produced);
            imp_records_release(&recs);
            if (!ok) return false;
        } else {
            int64_t source = imp_byte_position(&r);
            int64_t available;
            if ((source < 0) || (source > size)) return false;
            available = size - source;
            if (available > unpacked) available = unpacked;
            if (available > 0) xx_rt_memcpy(output + produced_total, directory + source, (size_t)available);
            if (available < unpacked) xx_rt_memset(output + produced_total + available, 0, (size_t)(unpacked - available));
        }

        chunk_sizes[chunks] = (uint32_t)unpacked;
        ++chunks;
        produced_total += (size_t)unpacked;
        total += unpacked;

        if (packed <= 0) break;
        offset += packed + 6;
        if ((total >= ((int64_t)records * XX_IMP_DIRECTORY_RECORD_SIZE)) || (offset >= size)) break;
    }

    if (chunks == 0) return false;
    if (chunk_count) *chunk_count = chunks;
    *written = produced_total;

    return true;
}
