/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Port of XArchive/Algos/xzxzipdecoder.cpp.  See the header for the two
 * codecs, the Hobeta wrapper and the inverted tree walk.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/zxzip/xx_zxzip.h"

#define ZX_RING_SIZE 0x2000
#define ZX_RING_MASK (ZX_RING_SIZE - 1)
#define ZX_SHRINK_CODES 0x2000
#define ZX_SHRINK_FIRST 0x101
#define ZX_SHRINK_ESCAPE 0x100
#define ZX_SHRINK_MAX_WIDTH 13
#define ZX_MODE_C_LIMIT 0x1600

/* A canonical tree over n symbols creates at most sum(lengths) + 1 nodes.  The
 * 64-symbol tables top out at 64 * 16 + 1 and the 256-symbol literal table at
 * 256 * 13 + 1, so 3400 covers every table here with room to spare. */
#define ZX_TREE_MAX 3400

#define ZX_METHOD_STORE 0
#define ZX_METHOD_SHRINK 2
#define ZX_METHOD_LZH 3

/* Static Huffman code-length tables, recovered from the reference at
 * VAs 0x8200ac..0x820154.  Read-only, so sharing them across threads is safe. */
static const uint8_t zx_a_len[64] = {
    1,  3,  3,  4,  5,  5,  5,  6,  6,  7,  7,  7,  7,  8,  8,  8,
    9,  9,  9,  9,  10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 12,
    12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 16, 16, 10
};

static const uint8_t zx_a_off[64] = {
    2, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

static const uint8_t zx_b_len[64] = {
    3,  2,  3,  3,  4,  4,  4,  5,  5,  6,  6,  7,  7,  7,  7,  8,
    8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10,
    10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 6
};

static const uint8_t zx_b_off[64] = {
    3, 3, 4, 4, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

static const uint8_t zx_c_lit[256] = {
    11, 12, 12, 12, 12, 12, 12, 12, 12, 8,  7,  12, 12, 7,  12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 12, 12, 12, 12, 12,
    4,  10, 8,  12, 10, 12, 10, 8,  7,  7,  8,  9,  7,  6,  7,  8,
    7,  6,  7,  7,  7,  7,  8,  7,  7,  8,  8,  12, 11, 7,  9,  11,
    12, 6,  7,  6,  6,  5,  7,  8,  8,  6,  11, 9,  6,  7,  6,  6,
    7,  11, 6,  6,  6,  7,  9,  8,  9,  9,  11, 8,  11, 9,  12, 8,
    12, 5,  6,  6,  6,  5,  6,  6,  6,  5,  11, 7,  5,  6,  5,  5,
    6,  10, 5,  5,  5,  5,  8,  7,  8,  8,  10, 11, 11, 12, 12, 12,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    13, 12, 13, 13, 13, 12, 13, 13, 13, 12, 13, 13, 13, 13, 12, 13,
    13, 13, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13
};

static const uint8_t zx_c_len[64] = {
    2,  3,  3,  3,  4,  4,  4,  5,  5,  6,  6,  7,  7,  7,  7,  8,
    8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10,
    10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 6
};

static const uint8_t zx_c_off[64] = {
    3, 3, 4, 4, 4, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

/* ---------------------------------------------------- LSB-first bits ----- */

typedef struct zx_bits {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t acc;
    int32_t count;
} zx_bits;

static void zx_bits_init(zx_bits *b, const uint8_t *data, size_t size)
{
    b->data = data;
    b->size = size;
    b->pos = 0;
    b->acc = 0;
    b->count = 0;
}

/* -1 means exhausted. */
static int32_t zx_bits_get(zx_bits *b, int32_t bits)
{
    int32_t value;

    while (b->count < bits) {
        if (b->pos >= b->size) return -1;
        b->acc |= ((uint32_t)b->data[b->pos]) << b->count;
        ++b->pos;
        b->count += 8;
    }
    value = (int32_t)(b->acc & ((1u << bits) - 1u));
    b->acc >>= bits;
    b->count -= bits;

    return value;
}

/* ------------------------------------------------------- decode tree ----- */

/* Nodes are pairs of children indexed by the CODE bit, a leaf is
 * (symbol | 0x8000) and 0 means "no child" - node 0 is the root, so it can
 * never legitimately appear as a child. */
typedef struct zx_tree {
    int32_t child0[ZX_TREE_MAX];
    int32_t child1[ZX_TREE_MAX];
    int32_t count;
    bool valid;
} zx_tree;

static bool zx_tree_insert(zx_tree *t, uint32_t code, int32_t length,
                           int32_t symbol)
{
    int32_t current = 0;
    int32_t position;

    for (position = length - 1; position >= 0; --position) {
        int32_t *children = ((code >> position) & 1u) ? t->child1 : t->child0;
        int32_t value = children[current];

        if (value & 0x8000) return false; /* a leaf where a branch is needed */
        if (value == 0) {
            if (position == 0) {
                children[current] = symbol | 0x8000;
            } else {
                int32_t node;
                if (t->count >= ZX_TREE_MAX) return false;
                node = t->count;
                t->child0[node] = 0;
                t->child1[node] = 0;
                ++t->count;
                children[current] = node;
                current = node;
            }
        } else {
            if (position == 0) return false; /* prefix clash */
            if (value >= t->count) return false;
            current = value;
        }
    }

    return true;
}

static bool zx_tree_build(zx_tree *t, const uint8_t *lengths, int32_t count)
{
    int32_t max_length = 0;
    int32_t i;
    int32_t length;
    uint32_t code = 0;

    xx_rt_memset(t, 0, sizeof(*t));
    t->count = 1; /* the root */

    for (i = 0; i < count; ++i) {
        if (lengths[i] > 24) return false;
        if ((int32_t)lengths[i] > max_length) max_length = lengths[i];
    }

    for (length = 1; length <= max_length; ++length) {
        int32_t symbol;
        for (symbol = 0; symbol < count; ++symbol) {
            if ((int32_t)lengths[symbol] == length) {
                if (!zx_tree_insert(t, code, length, symbol)) return false;
                ++code;
            }
        }
        code <<= 1;
    }
    t->valid = true;

    return true;
}

/* -1 on a bad code or an exhausted stream.
 * DELIBERATE: a stream bit of 0 takes the child stored for CODE bit 1.  The
 * reference inverts here and the canonical tables hide it; "fixing" this
 * decodes a plausible-looking but wrong stream. */
static int32_t zx_tree_decode(const zx_tree *t, zx_bits *bits)
{
    int32_t current = 0;
    int32_t step;

    if (!t->valid) return -1;

    for (step = 0; step < 32; ++step) {
        int32_t bit = zx_bits_get(bits, 1);
        int32_t value;
        if (bit < 0) return -1;
        value = (bit == 0) ? t->child1[current] : t->child0[current];
        if (value == 0) return -1;
        if (value & 0x8000) return value & 0x7fff;
        if ((value < 0) || (value >= t->count)) return -1;
        current = value;
    }

    return -1;
}

/* ------------------------------------------------------- method 3: LZH --- */

typedef struct zx_lzh_ctx {
    zx_tree length_tree;
    zx_tree offset_tree;
    zx_tree literal_tree;
    uint8_t ring[ZX_RING_SIZE];
} zx_lzh_ctx;

static bool zx_lzh_decode(zx_lzh_ctx *ctx, const uint8_t *input,
                          size_t input_size, size_t output_size,
                          uint8_t sub_method, uint8_t *output,
                          size_t output_capacity, size_t *produced_out)
{
    const uint8_t *length_table;
    const uint8_t *offset_table;
    const uint8_t *literal_table = NULL;
    int32_t bias = 2;
    int32_t shift = 6;
    zx_bits bits;
    int32_t position = 0;
    size_t produced = 0;
    int64_t left;
    bool use_literal_tree;

    *produced_out = 0;
    ctx->literal_tree.valid = false;

    if (sub_method == 0) {
        length_table = zx_a_len;
        offset_table = zx_a_off;
    } else if (output_size < ZX_MODE_C_LIMIT) {
        length_table = zx_b_len;
        offset_table = zx_b_off;
    } else {
        length_table = zx_c_len;
        offset_table = zx_c_off;
        literal_table = zx_c_lit;
        bias = 3;
        shift = 7;
    }

    if (!zx_tree_build(&ctx->length_tree, length_table, 64)) return false;
    if (!zx_tree_build(&ctx->offset_tree, offset_table, 64)) return false;
    if (literal_table && !zx_tree_build(&ctx->literal_tree, literal_table, 256)) {
        return false;
    }
    use_literal_tree = (literal_table != NULL);

    zx_bits_init(&bits, input, input_size);
    xx_rt_memset(ctx->ring, 0, ZX_RING_SIZE);

    left = (int64_t)output_size;

    while (left > 0) {
        int32_t flag = zx_bits_get(&bits, 1);
        if (flag < 0) return false;

        if (flag == 0) {
            int32_t low = zx_bits_get(&bits, shift);
            int32_t high;
            int32_t distance;
            int32_t length;
            int32_t source;
            int32_t i;

            if (low < 0) return false;
            high = zx_tree_decode(&ctx->offset_tree, &bits);
            if (high < 0) return false;
            distance = low + (high << shift);
            length = zx_tree_decode(&ctx->length_tree, &bits);
            if (length < 0) return false;
            if (length == 0x3f) {
                int32_t extra = zx_bits_get(&bits, 8);
                if (extra < 0) return false;
                length += extra;
            }
            source = (position - distance - 1) & ZX_RING_MASK;
            /* The reference lets a final match overshoot the stated length and
             * catches it afterwards against the PADDED size; bounding the
             * writes here rejects exactly the same streams. */
            for (i = 0; i < (length + bias); ++i) {
                uint8_t byte = ctx->ring[source];
                source = (source + 1) & ZX_RING_MASK;
                ctx->ring[position] = byte;
                position = (position + 1) & ZX_RING_MASK;
                if (produced >= output_capacity) return false;
                output[produced++] = byte;
                --left;
            }
        } else {
            int32_t byte;
            if (use_literal_tree) {
                byte = zx_tree_decode(&ctx->literal_tree, &bits);
                if (byte < 0) return false;
            } else {
                byte = zx_bits_get(&bits, 8);
                if (byte < 0) return false;
            }
            ctx->ring[position] = (uint8_t)byte;
            position = (position + 1) & ZX_RING_MASK;
            if (produced >= output_capacity) return false;
            output[produced++] = (uint8_t)byte;
            --left;
        }
    }

    *produced_out = produced;

    return true;
}

/* -------------------------------------------------- method 2: Shrink ----- */

typedef struct zx_shrink_ctx {
    int32_t prefix[ZX_SHRINK_CODES];
    uint8_t suffix[ZX_SHRINK_CODES];
    uint8_t free_code[ZX_SHRINK_CODES];
    uint8_t used[ZX_SHRINK_CODES];
    uint8_t stack[ZX_SHRINK_CODES];
} zx_shrink_ctx;

static bool zx_shrink_decode(zx_shrink_ctx *ctx, const uint8_t *input,
                             size_t input_size, uint8_t *output,
                             size_t output_size)
{
    zx_bits bits;
    int32_t width = 9;
    int32_t next_free = ZX_SHRINK_FIRST;
    int32_t previous = 0;
    bool have_previous = false;
    size_t produced = 0;
    int64_t left = (int64_t)output_size;
    int32_t i;

    xx_rt_memset(ctx->prefix, 0, sizeof(ctx->prefix));
    xx_rt_memset(ctx->suffix, 0, sizeof(ctx->suffix));
    xx_rt_memset(ctx->free_code, 0, sizeof(ctx->free_code));
    xx_rt_memset(ctx->used, 0, sizeof(ctx->used));
    xx_rt_memset(ctx->stack, 0, sizeof(ctx->stack));
    for (i = ZX_SHRINK_FIRST; i < ZX_SHRINK_CODES; ++i) ctx->free_code[i] = 1;

    zx_bits_init(&bits, input, input_size);

    while (left > 0) {
        int32_t code = zx_bits_get(&bits, width);
        int32_t stack_size = 0;
        int32_t c;
        int32_t fix = -1;
        int32_t seen = -1;
        int32_t k;

        if (code < 0) return false;
        if (code >= ZX_SHRINK_CODES) return false;
        if (ctx->free_code[code]) return false;

        if (code == ZX_SHRINK_ESCAPE) {
            int32_t sub = zx_bits_get(&bits, width);
            if (sub < 0) return false;
            if (sub == 1) {
                if (width < ZX_SHRINK_MAX_WIDTH) ++width;
            } else if (sub == 2) {
                /* partial clear: free every code that has no children */
                if (have_previous && (next_free >= 1)) {
                    ctx->free_code[next_free - 1] = 1;
                }
                xx_rt_memset(ctx->used, 0, sizeof(ctx->used));
                for (i = ZX_SHRINK_FIRST; i < ZX_SHRINK_CODES; ++i) {
                    if (!ctx->free_code[i]) {
                        int32_t parent = ctx->prefix[i];
                        if ((parent >= 0) && (parent < ZX_SHRINK_CODES)) {
                            ctx->used[parent] = 1;
                        }
                    }
                }
                for (i = ZX_SHRINK_FIRST; i < ZX_SHRINK_CODES; ++i) {
                    if (!ctx->used[i]) ctx->free_code[i] = 1;
                }
                next_free = ZX_SHRINK_FIRST;
                while ((next_free < ZX_SHRINK_CODES) &&
                       (ctx->free_code[next_free] == 0)) {
                    ++next_free;
                }
                if (next_free < ZX_SHRINK_CODES) {
                    have_previous = true;
                    ctx->free_code[next_free] = 0;
                    ctx->prefix[next_free] = previous;
                    ++next_free;
                }
            } else {
                return false;
            }
            continue;
        }

        c = code;
        while (c > 0xff) {
            if ((c >= ZX_SHRINK_CODES) || (stack_size >= (ZX_SHRINK_CODES - 1))) {
                return false;
            }
            if (c == (next_free - 1)) seen = stack_size;
            ctx->stack[stack_size++] = ctx->suffix[c];
            fix = seen;
            c = ctx->prefix[c];
        }
        if ((c < 0) || (stack_size >= ZX_SHRINK_CODES)) return false;
        ctx->stack[stack_size++] = (uint8_t)c;
        if (have_previous && (next_free >= 1)) {
            ctx->suffix[next_free - 1] = (uint8_t)c;
            if (fix >= 0) ctx->stack[fix] = (uint8_t)c;
        }
        for (k = stack_size - 1; k >= 0; --k) {
            if (produced >= output_size) return false;
            output[produced++] = ctx->stack[k];
            --left;
        }

        while ((next_free < ZX_SHRINK_CODES) &&
               (ctx->free_code[next_free] == 0)) {
            ++next_free;
        }
        have_previous = (next_free < ZX_SHRINK_CODES);
        previous = code;
        if (have_previous) {
            ctx->free_code[next_free] = 0;
            ctx->prefix[next_free] = code;
            ++next_free;
        }
    }

    return (left == 0) && (produced == output_size);
}

/* ------------------------------------------------------- the wrapper ----- */

bool xx_zxzip_member_size(const uint8_t *entry, size_t entry_size,
                          size_t *data_size, size_t *padded_size)
{
    uint8_t type;
    uint64_t start;
    uint64_t length;
    uint64_t sectors;
    uint64_t size;
    uint64_t rounded;

    if (data_size) *data_size = 0;
    if (padded_size) *padded_size = 0;
    if (!entry || (entry_size != XX_ZXZIP_ENTRY_SIZE)) return false;

    type = entry[8];
    start = (uint64_t)entry[9] | ((uint64_t)entry[10] << 8);
    length = (uint64_t)entry[11] | ((uint64_t)entry[12] << 8);
    sectors = entry[13];

    size = ((type == 'B') || (type == 'b')) ? (start + 4) : length;
    rounded = (size + 0xff) & ~(uint64_t)0xff;
    if (rounded != (sectors * 256)) {
        size = sectors * 256;
        rounded = size;
    }
    if (rounded > XX_ZXZIP_MAX_UNCOMPRESSED_SIZE) return false;

    if (data_size) *data_size = (size_t)size;
    if (padded_size) *padded_size = (size_t)rounded;

    return true;
}

bool xx_zxzip_hobeta_header(const uint8_t *entry, size_t entry_size,
                            uint8_t *header)
{
    uint32_t sum = 0;
    uint32_t check;
    int32_t i;

    if (!entry || !header || (entry_size != XX_ZXZIP_ENTRY_SIZE)) return false;

    for (i = 0; i < 13; ++i) header[i] = entry[i];
    header[13] = 0;
    header[14] = entry[13]; /* the sector count */

    for (i = 0; i < 15; ++i) sum += header[i];
    check = (((sum & 0xffffu) * 0x101u) + 0x69u) & 0xffffu;
    header[15] = (uint8_t)(check & 0xffu);
    header[16] = (uint8_t)((check >> 8) & 0xffu);

    return true;
}

bool xx_zxzip_decode_memory(const uint8_t *input, size_t input_size,
                            const uint8_t *entry, size_t entry_size,
                            uint8_t *output, size_t output_size,
                            size_t *written)
{
    size_t data_size = 0;
    size_t padded_size = 0;
    size_t total;
    size_t produced = 0;
    uint8_t method;
    uint8_t sub_method;
    uint8_t *data;
    bool ok = false;

    if (written) *written = 0;
    if (!entry || !output) return false;
    if (entry_size != XX_ZXZIP_ENTRY_SIZE) return false;
    if (!input && (input_size != 0)) return false;

    if (!xx_zxzip_member_size(entry, entry_size, &data_size, &padded_size)) {
        return false;
    }
    if (data_size > padded_size) return false;

    total = (size_t)XX_ZXZIP_HOBETA_SIZE + padded_size;
    if (output_size < total) return false;

    if (!xx_zxzip_hobeta_header(entry, entry_size, output)) return false;

    method = entry[0x14];
    sub_method = entry[0x15];
    data = output + XX_ZXZIP_HOBETA_SIZE;

    if (method == ZX_METHOD_STORE) {
        if (input_size != data_size) return false;
        if (data_size != 0) xx_rt_memcpy(data, input, data_size);
        produced = data_size;
        ok = true;
    } else if (method == ZX_METHOD_SHRINK) {
        zx_shrink_ctx *ctx = (zx_shrink_ctx *)xx_mem_alloc(sizeof(zx_shrink_ctx));
        if (!ctx) return false;
        ok = zx_shrink_decode(ctx, input, input_size, data, data_size);
        xx_mem_free(ctx);
        if (!ok) return false;
        produced = data_size;
    } else if (method == ZX_METHOD_LZH) {
        zx_lzh_ctx *ctx = (zx_lzh_ctx *)xx_mem_alloc(sizeof(zx_lzh_ctx));
        if (!ctx) return false;
        ok = zx_lzh_decode(ctx, input, input_size, data_size, sub_method,
                           data, padded_size, &produced);
        xx_mem_free(ctx);
        if (!ok) return false;
    } else {
        /* method 1 is unreachable - see the header */
        return false;
    }

    if (produced < data_size) return false;
    if (produced > padded_size) return false;

    if (produced < padded_size) {
        xx_rt_memset(data + produced, 0, padded_size - produced);
    }

    if (written) *written = total;

    return true;
}
