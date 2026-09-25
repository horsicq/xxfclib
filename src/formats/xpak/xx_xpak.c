/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * XPAK: one 25-byte header and one packed stream.  xx_xpak.h carries the
 * field table.
 *
 * The codec is an LZ77 whose literals, end code and match classes share one
 * adaptive Huffman tree.  Its decoder was recovered from the 16-bit routines
 * of 3Com's COMSLINK INST.EXE (code segment 0x831: 0x0A32 builds the tree,
 * 0x07E9 adds a leaf, 0x0846 updates a weight, 0x13F8 grows the alphabet,
 * 0x125F reads one symbol).  The model below keeps that code's exact memory
 * layout - a 0x400-byte symbol table followed by 6-byte nodes {parent link,
 * weight, child} - because its quirks decide every later code, and a model
 * that is only "equivalent" drifts off the stream after a few thousand
 * symbols.  Every address the model touches is bounds-checked against the
 * 0x1C00-byte arena the original clears, and every loop is step-bounded, so
 * a hostile stream can corrupt nothing and cannot spin.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xpak/xx_xpak.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as XPAK is registered there. */
#ifdef XPAK
#define XX_XPAK_FILE_TYPE XX_FILE_TYPE_XPAK
#else
#define XX_XPAK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XPAK_HEADER_SIZE 25
#define XPAK_NAME_OFFSET 8
#define XPAK_SIZE_OFFSET 21
/* The unpacked size is a u32, so this is the widest member the container can
 * describe at all; the cap only rules out a field that is plainly garbage. */
#define XPAK_MAX_UNPACKED INT64_C(0x10000000)

/* Codec stream: 9 header bytes, the start marker, then the bit stream. */
#define XPAK_CODEC_HEADER 9U
#define XPAK_CODEC_MAGIC 0xFEFFU
#define XPAK_MARKER 0xFFU
/* Longest codec prefix check_is_valid ever reads: header, a skip count of
 * 0xFE and the skipped bytes. */
#define XPAK_CODEC_PREFIX_MAX (XPAK_CODEC_HEADER + 1U + 0xFEU)

/* Model arena: symbol table (2 bytes per symbol) below ROOT, 6-byte nodes
 * from ROOT on.  The widest legal alphabet is 257 + 15 * 15 = 482 symbols,
 * 963 nodes, which ends at 0x400 + 963 * 6 = 0x1A92. */
#define XPAK_ROOT 0x400U
#define XPAK_ARENA 0x1C00U
#define XPAK_EOF 0x100U
#define XPAK_NODE 6U
#define XPAK_MAX_DEPTH 1024U
#define XPAK_STEP_LIMIT 4000000UL

/* Output staging.  A distance is at most 0x7FFF and a match at most 0x8000
 * bytes, so keeping 0x8000 bytes of history after a flush always suffices
 * and a flushed window always has room for the next match. */
#define XPAK_WINDOW 0x20000U
#define XPAK_KEEP 0x8000U
#define XPAK_MAX_MATCH 0x8000U
#define XPAK_INPUT_BUFFER 4096U

typedef struct xpak_context_s {
    char name[XX_XPAK_NAME_FIELD + 1];
    int64_t input_size;
    int64_t declared_size;
    int64_t archive_size;  /**< Clamped to what is actually present. */
    int64_t stream_offset;
    int64_t stream_size;
    int64_t unpacked_size;
    bool truncated;
} xpak_context;

typedef struct xpak_stream_s {
    xpak_context context;
    size_t index;
    size_t count;
} xpak_stream;

typedef struct xpak_codec_s {
    uint32_t window;
    uint32_t max_match;
    uint32_t limit;
    uint32_t data_offset; /**< First bit-stream byte, from the codec header. */
} xpak_codec;

typedef struct xpak_model_s {
    uint8_t memory[XPAK_ARENA];
    uint32_t window;
    uint32_t limit;
    uint32_t classes_divisor; /**< Length classes per distance class. */
    uint32_t count;           /**< Root increments since the last rescale. */
    uint32_t zero;            /**< Parent of the zero-weight region. */
    uint32_t symbols;
    uint32_t nodes;
    unsigned long steps;
    bool bad;
} xpak_model;

typedef struct xpak_input_s {
    const uint8_t *memory;
    xx_io_device *device;
    int64_t offset;         /**< Device offset of the next refill. */
    uint64_t remaining;     /**< Stream bytes not yet buffered. */
    uint64_t fetched;       /**< Bytes handed to the bit accumulator. */
    size_t length;
    size_t position;
    uint32_t bits;
    uint32_t count;
    uint8_t buffer[XPAK_INPUT_BUFFER];
} xpak_input;

typedef struct xpak_output_s {
    uint8_t *window;
    size_t length;      /**< Bytes staged in window. */
    size_t delivered;   /**< Leading window bytes already delivered. */
    uint64_t total;     /**< Bytes produced so far. */
    uint64_t expected;
    xx_io_device *device;
    uint8_t *memory;    /**< Memory sink of exactly expected bytes. */
    uint64_t written;
} xpak_output;

static uint32_t xpak_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint32_t xpak_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static bool xpak_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Codec header                                                            */

/* The decoder's own acceptance rules (INST.EXE 0x146E): the 0xFEFF word, the
 * three ranges, and a start marker that is 0xFF itself or a skip count N in
 * 1..0xFE whose N-th following byte is 0xFF.  At least one bit-stream byte
 * must follow, because even an empty member needs its end code. */
static bool xpak_codec_parse(const uint8_t *head, size_t available,
                             uint64_t stream_size, xpak_codec *out) {
    xpak_codec codec;
    uint32_t marker;
    if (!head || !out || available < XPAK_CODEC_HEADER + 1U ||
        xpak_le16(head + 1U) != XPAK_CODEC_MAGIC)
        return false;
    codec.window = xpak_le16(head + 3U);
    codec.max_match = xpak_le16(head + 5U);
    codec.limit = xpak_le16(head + 7U);
    if (codec.window < 0x200U || codec.window > 0x8000U ||
        codec.max_match < 0x100U || codec.max_match > 0x4000U ||
        codec.limit < 0x200U || codec.limit > 0x8000U)
        return false;
    marker = head[XPAK_CODEC_HEADER];
    if (marker == XPAK_MARKER) {
        codec.data_offset = XPAK_CODEC_HEADER + 1U;
    } else {
        if (marker == 0U || available < XPAK_CODEC_HEADER + 1U + marker ||
            head[XPAK_CODEC_HEADER + marker] != XPAK_MARKER)
            return false;
        codec.data_offset = XPAK_CODEC_HEADER + 1U + marker;
    }
    if (stream_size <= (uint64_t)codec.data_offset) return false;
    *out = codec;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Model                                                                   */

static uint32_t xm_rw(xpak_model *x, uint32_t address) {
    if (address > XPAK_ARENA - 2U) {
        x->bad = true;
        return 0U;
    }
    return (uint32_t)x->memory[address] |
           ((uint32_t)x->memory[address + 1U] << 8U);
}

static void xm_ww(xpak_model *x, uint32_t address, uint32_t value) {
    if (address > XPAK_ARENA - 2U) {
        x->bad = true;
        return;
    }
    x->memory[address] = (uint8_t)value;
    x->memory[address + 1U] = (uint8_t)(value >> 8U);
}

static bool xm_step(xpak_model *x) {
    return !x->bad && ++x->steps <= XPAK_STEP_LIMIT;
}

/* Parent links of the children of node @p node (@p child is its child
 * field): a pair of nodes gets node * 2 and node * 2 + 1, a leaf's symbol
 * slot gets the node itself. */
static void xm_adopt(xpak_model *x, uint32_t child, uint32_t node) {
    if (child >= XPAK_ROOT) {
        uint32_t link = (node << 1U) & 0xFFFFU;
        xm_ww(x, child, link);
        xm_ww(x, child + XPAK_NODE, link | 1U);
    } else {
        xm_ww(x, child, node);
    }
}

/* 0x07E9: split the first zero-weight leaf of the trailing leaf run into
 * its old symbol and the new one, appending the pair at *node. */
static void xm_add_leaf(xpak_model *x, uint32_t *node, uint32_t *slot) {
    uint32_t fresh = *node, scan = fresh - XPAK_NODE, split, old, weight,
             link;
    for (;;) {
        split = scan;
        if (!xm_step(x) || xm_rw(x, scan + 2U) != 0U) break;
        scan -= XPAK_NODE;
        if (xm_rw(x, scan + 4U) >= XPAK_ROOT) break;
    }
    old = xm_rw(x, split + 4U);
    xm_ww(x, old, fresh);
    xm_ww(x, split + 4U, fresh);
    weight = xm_rw(x, split + 2U);
    link = (split << 1U) & 0xFFFFU;
    xm_ww(x, fresh, link);
    xm_ww(x, fresh + 2U, weight);
    xm_ww(x, fresh + 4U, old);
    xm_ww(x, fresh + XPAK_NODE, link | 1U);
    xm_ww(x, fresh + XPAK_NODE + 4U, *slot);
    xm_ww(x, *slot, fresh + XPAK_NODE);
    *node = fresh + 2U * XPAK_NODE;
    *slot += 2U;
    ++x->symbols;
    x->nodes += 2U;
}

/* 0x0A32: root, end code and literal 0, then literals 1..255. */
static bool xm_init(xpak_model *x, const xpak_codec *codec) {
    uint32_t node, slot, index, span;
    xx_mem_zero(x, sizeof(*x));
    x->window = codec->window;
    x->limit = codec->limit;
    span = codec->max_match - 1U;
    x->classes_divisor = 1U;
    while (span) {
        ++x->classes_divisor;
        span >>= 1U;
    }
    x->zero = XPAK_ROOT;
    xm_ww(x, XPAK_ROOT + 4U, XPAK_ROOT + XPAK_NODE);
    xm_ww(x, XPAK_ROOT, 0xFFFFU);
    node = XPAK_ROOT + XPAK_NODE;
    xm_ww(x, node, XPAK_ROOT << 1U);
    xm_ww(x, node + 4U, XPAK_EOF * 2U);
    xm_ww(x, XPAK_EOF * 2U, node);
    node += XPAK_NODE;
    xm_ww(x, node, (XPAK_ROOT << 1U) | 1U);
    xm_ww(x, node + 4U, 0U);
    xm_ww(x, 0U, node);
    x->symbols = 2U;
    x->nodes = 3U;
    node += XPAK_NODE;
    slot = 2U;
    for (index = 0U; index < 0xFFU; ++index) xm_add_leaf(x, &node, &slot);
    return !x->bad;
}

/* 0x09A7: halve every weight, rebuild the internal weights from the zero
 * region's parent back to the root, and restore the ordering. */
static void xm_rescale(xpak_model *x) {
    uint32_t index, node, pair, sum, parent, above, weight, run, first,
             temp, child;
    node = XPAK_ROOT;
    for (index = 0U; index < x->nodes && !x->bad; ++index) {
        xm_ww(x, node + 2U, ((xm_rw(x, node + 2U) + 1U) & 0xFFFFU) >> 1U);
        node += XPAK_NODE;
    }
    pair = xm_rw(x, x->zero + 4U) + XPAK_NODE;
    for (;;) {
        if (!xm_step(x)) return;
        sum = xm_rw(x, pair + 2U);
        pair -= XPAK_NODE;
        sum = (sum + xm_rw(x, pair + 2U)) & 0xFFFFU;
        parent = xm_rw(x, pair) >> 1U;
        pair -= XPAK_NODE;
        xm_ww(x, parent + 2U, sum);
        if (x->bad) return;
        if (parent == XPAK_ROOT) {
            x->count = xm_rw(x, parent + 2U);
            return;
        }
        for (;;) {
            if (!xm_step(x)) return;
            above = parent;
            parent -= XPAK_NODE;
            if (xm_rw(x, parent + 2U) >= sum) break;
            weight = xm_rw(x, parent + 2U);
            do {
                first = parent;
                parent -= XPAK_NODE;
                if (!xm_step(x)) return;
            } while (xm_rw(x, parent + 2U) == weight);
            parent = above;
            temp = xm_rw(x, parent + 2U);
            xm_ww(x, parent + 2U, xm_rw(x, first + 2U));
            xm_ww(x, first + 2U, temp);
            temp = xm_rw(x, parent + 4U);
            xm_ww(x, parent + 4U, xm_rw(x, first + 4U));
            xm_ww(x, first + 4U, temp);
            xm_adopt(x, xm_rw(x, parent + 4U), parent);
            /* The moved node is internal, so its child is always a pair. */
            child = xm_rw(x, first + 4U);
            run = (first << 1U) & 0xFFFFU;
            xm_ww(x, child, run);
            xm_ww(x, child + XPAK_NODE, run | 1U);
            parent = first;
            sum = xm_rw(x, parent + 2U);
        }
    }
}

/* 0x0846: count one occurrence of @p symbol. */
static bool xm_update(xpak_model *x, uint32_t symbol) {
    uint32_t bx, di, si, cx, ax, temp, last, bound;
    bx = xm_rw(x, symbol * 2U);
    if (bx >= x->zero) goto special;
walk:
    if (!xm_step(x)) return false;
    xm_ww(x, bx + 2U, xm_rw(x, bx + 2U) + 1U);
    di = bx - XPAK_NODE;
    cx = xm_rw(x, di + 2U);
    if (xm_rw(x, bx + 2U) <= cx) goto parent;
    do {
        si = di;
        di -= XPAK_NODE;
        if (!xm_step(x)) return false;
    } while (xm_rw(x, di + 2U) == cx);
    temp = xm_rw(x, si + 2U);
    xm_ww(x, si + 2U, xm_rw(x, bx + 2U));
    xm_ww(x, bx + 2U, temp);
    di = xm_rw(x, si + 4U);
    temp = xm_rw(x, bx + 4U);
    xm_ww(x, bx + 4U, di);
    di = temp;
    xm_ww(x, si + 4U, di);
    ax = si;
    xm_adopt(x, di, si);
    di = xm_rw(x, bx + 4U);
    si = bx;
    bx = ax;
    xm_adopt(x, di, si);
parent:
    if (!xm_step(x)) return false;
    bx = xm_rw(x, bx) >> 1U;
    if (xm_rw(x, bx) != 0xFFFFU) goto walk;
root:
    xm_ww(x, bx + 2U, xm_rw(x, bx + 2U) + 1U);
    x->count = (x->count + 1U) & 0xFFFFU;
    if (x->count > x->limit) xm_rescale(x);
    return !x->bad;
special:
    cx = xm_rw(x, bx + 2U);
    if (cx == 0U) goto first_use;
    if (xm_rw(x, bx - 10U) == cx) {
        si = bx - 2U * XPAK_NODE;
        di = xm_rw(x, bx + 4U);
        xm_ww(x, di, si);
        temp = xm_rw(x, si + 4U);
        xm_ww(x, si + 4U, di);
        di = temp;
        xm_ww(x, bx + 4U, di);
        xm_ww(x, di, bx);
        bx = si;
        goto walk;
    }
    xm_ww(x, bx + 2U, xm_rw(x, bx + 2U) + 1U);
    goto parent;
first_use:
    ax = xm_rw(x, bx + 4U);
    si = x->zero;
    di = xm_rw(x, si + 4U);
    if (si == XPAK_ROOT) {
        if (xm_rw(x, di + 2U) != cx) di = xm_rw(x, di + XPAK_NODE + 4U);
    } else {
        di += XPAK_NODE;
        if (di == bx) goto promote;
        di = xm_rw(x, di + 4U);
    }
    if (x->bad) return false;
    /* Open a slot at di: shift the parent links behind it by one node and
     * the child fields between it and the leaf up by one node. */
    last = ((x->nodes - 1U) * XPAK_NODE + XPAK_ROOT) & 0xFFFFU;
    bound = di + XPAK_NODE;
    for (temp = last; temp > bound; temp -= XPAK_NODE) {
        if (!xm_step(x)) return false;
        xm_ww(x, temp, xm_rw(x, temp) + 12U);
    }
    si = bx - XPAK_NODE;
    while (si >= di) {
        uint32_t moved;
        if (!xm_step(x)) return false;
        moved = xm_rw(x, si + 4U);
        xm_ww(x, bx + 4U, moved);
        if (moved < XPAK_ROOT) xm_ww(x, moved, bx);
        if (si < XPAK_NODE) {
            x->bad = true;
            return false;
        }
        si -= XPAK_NODE;
        bx -= XPAK_NODE;
    }
    xm_ww(x, di + 4U, ax);
    xm_ww(x, ax, di);
    bx = di;
promote:
    if (!xm_step(x)) return false;
    xm_ww(x, bx + 2U, xm_rw(x, bx + 2U) + 1U);
    bx = xm_rw(x, bx) >> 1U;
    x->zero = bx;
    if (bx == XPAK_ROOT) goto root;
    goto walk;
}

/* 0x13F8: before each symbol, make sure a distance class exists for every
 * distance the output so far allows; each new class brings one symbol per
 * length class, and the shortest few start with small weights. */
static bool xm_grow(xpak_model *x, uint64_t produced) {
    uint32_t reach, needed, have, node, slot, first, seed, index;
    if (produced == 0U) return true;
    for (;;) {
        reach = produced + 1U >= (uint64_t)x->window ? x->window
                                                      : (uint32_t)produced + 1U;
        --reach;
        needed = 0U;
        while (reach) {
            ++needed;
            reach >>= 1U;
        }
        have = (x->symbols - 1U - XPAK_EOF) / x->classes_divisor + 1U;
        if (needed < have) return true;
        node = (x->nodes * XPAK_NODE + XPAK_ROOT) & 0xFFFFU;
        slot = x->symbols * 2U;
        first = x->symbols;
        for (index = 0U; index < x->classes_divisor; ++index)
            xm_add_leaf(x, &node, &slot);
        if (x->bad) return false;
        seed = x->classes_divisor > have ? have : x->classes_divisor;
        while (seed) {
            for (index = 0U; index < seed; ++index)
                if (!xm_update(x, first)) return false;
            ++first;
            --seed;
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Bit input and byte output                                               */

static bool xpak_next_byte(xpak_input *in, uint32_t *value) {
    if (in->position == in->length) {
        size_t amount;
        if (in->remaining == 0U) return false;
        amount = in->remaining < (uint64_t)XPAK_INPUT_BUFFER
                     ? (size_t)in->remaining : XPAK_INPUT_BUFFER;
        if (in->memory) {
            xx_rt_memcpy(in->buffer, in->memory, amount);
            in->memory += amount;
        } else if (!xpak_read_at(in->device, in->offset, in->buffer, amount)) {
            return false;
        }
        in->offset += (int64_t)amount;
        in->remaining -= amount;
        in->length = amount;
        in->position = 0U;
    }
    *value = in->buffer[in->position++];
    ++in->fetched;
    return true;
}

static bool xpak_bit(xpak_input *in, uint32_t *bit) {
    if (in->count == 0U) {
        if (!xpak_next_byte(in, &in->bits)) return false;
        in->count = 8U;
    }
    *bit = in->bits & 1U;
    in->bits >>= 1U;
    --in->count;
    return true;
}

/* A leading 1 followed by @p width bits, first bit most significant. */
static bool xpak_extra(xpak_input *in, uint32_t width, uint32_t *value) {
    uint32_t result = 1U, bit;
    while (width--) {
        if (!xpak_bit(in, &bit)) return false;
        result = (result << 1U) | bit;
    }
    *value = result;
    return true;
}

static bool xpak_deliver(xpak_output *out) {
    size_t amount = out->length - out->delivered;
    const uint8_t *data = out->window + out->delivered;
    if (amount == 0U) return true;
    if (out->memory) {
        if ((uint64_t)amount > out->expected - out->written) return false;
        xx_rt_memcpy(out->memory + out->written, data, amount);
    } else if (out->device) {
        size_t done = 0U;
        while (done < amount) {
            ssize_t wrote = xx_io_write(out->device, data + done,
                                        amount - done);
            if (wrote <= 0 || (size_t)wrote > amount - done) return false;
            done += (size_t)wrote;
        }
    }
    out->written += amount;
    out->delivered = out->length;
    return true;
}

/* Deliver what is staged and keep the last XPAK_KEEP bytes as history. */
static bool xpak_make_room(xpak_output *out, size_t needed) {
    if (out->length + needed <= XPAK_WINDOW) return true;
    if (!xpak_deliver(out)) return false;
    if (out->length > XPAK_KEEP) {
        xx_rt_memmove(out->window, out->window + out->length - XPAK_KEEP,
                      XPAK_KEEP);
        out->length = XPAK_KEEP;
        out->delivered = XPAK_KEEP;
    }
    return out->length + needed <= XPAK_WINDOW;
}

/* ---------------------------------------------------------------------- */
/* Decoder                                                                 */

static bool xpak_decode(xpak_input *in, const xpak_codec *codec,
                        xpak_output *out, xx_pd_struct *pd) {
    xpak_model *model;
    uint32_t node, bit, symbol, value, distance, length, depth, skip;
    unsigned long symbols = 0UL;
    bool result = false;
    model = (xpak_model *)xx_mem_alloc(sizeof(*model));
    out->window = (uint8_t *)xx_mem_alloc(XPAK_WINDOW);
    if (!model || !out->window || !xm_init(model, codec)) goto done;
    /* Step past the start marker (and a skipped prefix). */
    for (skip = XPAK_CODEC_HEADER; skip < codec->data_offset; ++skip)
        if (!xpak_next_byte(in, &value)) goto done;
    in->count = 0U;
    for (;;) {
        if ((++symbols & 0xFFFUL) == 0UL && pd && xx_pd_is_stopped(pd))
            goto done;
        model->steps = 0UL;
        if (!xm_grow(model, out->total)) goto done;
        node = xm_rw(model, XPAK_ROOT + 4U);
        for (depth = 0U;; ++depth) {
            if (depth >= XPAK_MAX_DEPTH || !xpak_bit(in, &bit)) goto done;
            if (bit) node += XPAK_NODE;
            node = xm_rw(model, node + 4U);
            if (model->bad) goto done;
            if (node <= XPAK_ROOT) break;
        }
        if ((node & 1U) != 0U || (symbol = node >> 1U) >= model->symbols)
            goto done;
        model->steps = 0UL;
        if (!xm_update(model, symbol)) goto done;
        if (symbol == XPAK_EOF) break;
        if (symbol < XPAK_EOF) {
            if (out->total >= out->expected || !xpak_make_room(out, 1U))
                goto done;
            out->window[out->length++] = (uint8_t)symbol;
            ++out->total;
            continue;
        }
        value = symbol - XPAK_EOF - 1U;
        if (!xpak_extra(in, value / model->classes_divisor, &distance) ||
            !xpak_extra(in, value % model->classes_divisor, &length))
            goto done;
        ++length;
        if (length > XPAK_MAX_MATCH ||
            (uint64_t)length > out->expected - out->total ||
            !xpak_make_room(out, length) || distance > out->length)
            goto done;
        {
            uint8_t *target = out->window + out->length;
            const uint8_t *source = target - distance;
            uint32_t index;
            for (index = 0U; index < length; ++index) target[index] = source[index];
        }
        out->length += length;
        out->total += length;
    }
    result = out->total == out->expected && xpak_deliver(out) &&
             out->written == out->expected;
done:
    if (model) xx_mem_free(model);
    if (out->window) xx_mem_free(out->window);
    out->window = NULL;
    return result;
}

bool xx_xpak_decode_memory(const uint8_t *stream, size_t stream_size,
                           uint8_t *output, size_t output_size,
                           size_t *consumed) {
    xpak_codec codec;
    xpak_input *in;
    xpak_output out;
    bool result;
    if (consumed) *consumed = 0U;
    if (!stream || (!output && output_size != 0U) ||
        !xpak_codec_parse(stream, stream_size, (uint64_t)stream_size, &codec))
        return false;
    in = (xpak_input *)xx_mem_calloc(1U, sizeof(*in));
    if (!in) return false;
    in->memory = stream + XPAK_CODEC_HEADER;
    in->remaining = (uint64_t)(stream_size - XPAK_CODEC_HEADER);
    xx_mem_zero(&out, sizeof(out));
    out.expected = (uint64_t)output_size;
    out.memory = output;
    result = xpak_decode(in, &codec, &out, NULL);
    if (result && consumed)
        *consumed = (size_t)in->fetched + XPAK_CODEC_HEADER;
    xx_mem_free(in);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Container                                                               */

/* The name field is read at its FIELD size, never size - 1.  A name has at
 * least one printable byte, is terminated inside the field, and everything
 * behind the terminator is zero: stale bytes there would mean this is not an
 * XPAK header at all. */
static bool xpak_decode_name(const uint8_t *field, char *out) {
    int32_t terminator = -1, index;
    for (index = 0; index < XX_XPAK_NAME_FIELD; ++index) {
        if (field[index] == 0U) {
            terminator = index;
            break;
        }
        if (field[index] < 0x20U || field[index] > 0x7eU ||
            field[index] == '/' || field[index] == '\\' ||
            field[index] == ':') return false;
        out[index] = (char)field[index];
    }
    if (terminator < 1) return false;
    for (index = terminator; index < XX_XPAK_NAME_FIELD; ++index)
        if (field[index] != 0U) return false;
    out[terminator] = 0;
    return true;
}

static char xpak_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the first @p stem bytes of @p name spell the upper-case
 * @p word exactly, ignoring case. */
static bool xpak_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || xpak_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* Extraction writes <base>/<name>.  The parser already refused separators,
 * drive colons and control bytes; this also refuses names that Windows
 * would resolve to "." or ".." (only dots and spaces), the other reserved
 * punctuation, and device names such as CON, LPT1.EXT or CONIN$, with or
 * without an extension and in any case. */
static bool xpak_safe_output_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length, stem = 0U, index;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xpak_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((xpak_upper(name[0]) == 'C' && xpak_upper(name[1]) == 'O' &&
          xpak_upper(name[2]) == 'M') ||
         (xpak_upper(name[0]) == 'L' && xpak_upper(name[1]) == 'P' &&
          xpak_upper(name[2]) == 'T')))
        return false;
    return true;
}

static void xpak_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool xpak_parse(Abstractformat *format, xpak_context *out,
                       xpak_codec *codec_out) {
    uint8_t header[XPAK_HEADER_SIZE];
    uint8_t head[XPAK_CODEC_PREFIX_MAX];
    xpak_context context;
    xpak_codec codec;
    int64_t total, size;
    size_t head_size;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= (int64_t)XPAK_HEADER_SIZE ||
        !xpak_read_at(format->device, format->base_address, header,
                      sizeof(header))) return false;
    if (xx_rt_memcmp(header, "XPAK", 4U) != 0) return false;
    xx_mem_zero(&context, sizeof(context));
    if (!xpak_decode_name(header + XPAK_NAME_OFFSET, context.name))
        return false;
    context.input_size = size;
    context.declared_size = (int64_t)xpak_le32(header + 4U);
    context.unpacked_size = (int64_t)xpak_le32(header + XPAK_SIZE_OFFSET);
    /* The archive size counts this header; the stream needs its codec
     * header, the marker and at least one byte of bits. */
    if (context.declared_size <=
        (int64_t)(XPAK_HEADER_SIZE + XPAK_CODEC_HEADER + 1U)) return false;
    if (context.unpacked_size > XPAK_MAX_UNPACKED) return false;
    /* A declared size longer than the file is a truncated archive, not a
     * wrong one: the header is intact and the member is still named and
     * measured.  The extent published is what is actually present. */
    context.truncated = context.declared_size > size;
    context.archive_size = context.truncated ? size : context.declared_size;
    context.stream_offset = format->base_address + XPAK_HEADER_SIZE;
    context.stream_size = context.archive_size - XPAK_HEADER_SIZE;
    if (context.stream_size <= (int64_t)(XPAK_CODEC_HEADER + 1U)) return false;
    head_size = context.stream_size < (int64_t)sizeof(head)
                    ? (size_t)context.stream_size : sizeof(head);
    if (!xpak_read_at(format->device, context.stream_offset, head, head_size) ||
        !xpak_codec_parse(head, head_size, (uint64_t)context.stream_size,
                          &codec))
        return false;
    *out = context;
    if (codec_out) *codec_out = codec;
    return true;
}

static bool xpak_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xpak_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool xpak_set_record(xx_archive_record *record,
                            const xpak_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->stream_offset - XPAK_HEADER_SIZE;
    record->header_size = XPAK_HEADER_SIZE;
    record->data_offset = context->stream_offset;
    record->compressed_size = context->stream_size;
    return xx_archive_record_set_original_name(record, context->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool xpak_unpack_context(Abstractformat *format,
                                const xpak_context *context,
                                xx_io_device *destination, xx_pd_struct *pd) {
    xpak_codec codec;
    xpak_context check;
    xpak_input *in;
    xpak_output out;
    bool result;
    if (!format || !context || context->truncated ||
        !xpak_parse(format, &check, &codec) ||
        check.stream_offset != context->stream_offset ||
        check.stream_size != context->stream_size ||
        check.unpacked_size != context->unpacked_size)
        return false;
    in = (xpak_input *)xx_mem_calloc(1U, sizeof(*in));
    if (!in) return false;
    in->device = format->device;
    in->offset = context->stream_offset + (int64_t)XPAK_CODEC_HEADER;
    in->remaining = (uint64_t)context->stream_size - XPAK_CODEC_HEADER;
    xx_mem_zero(&out, sizeof(out));
    out.expected = (uint64_t)context->unpacked_size;
    out.device = destination;
    result = xpak_decode(in, &codec, &out, pd);
    xx_mem_free(in);
    return result;
}

void xx_xpak_init(xx_xpak *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_XPAK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-xpak");
    xx_format_set_extension(&archive->format, "xpak");
    archive->format.check_is_valid = xx_xpak_check_is_valid;
    archive->format.handle_base_info = xx_xpak_handle_base_info;
    archive->format.get_format_size = xx_xpak_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_xpak_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_xpak_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_xpak_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_xpak_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_xpak_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_xpak_free_archive_records_reading;
    archive->declared_size = -1;
}

xx_xpak *xx_xpak_create(xx_io_device *device, int64_t base_address) {
    xx_xpak *archive = (xx_xpak *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_xpak_init(archive, device, base_address);
    return archive;
}

void xx_xpak_destroy(xx_xpak *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_xpak_free(xx_xpak *archive) {
    if (!archive) return;
    xx_xpak_destroy(archive);
    xx_mem_free(archive);
}

bool xx_xpak_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    xpak_context context;
    (void)pd;
    return xpak_parse(format, &context, NULL);
}

bool xx_xpak_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xpak_context context;
    xx_xpak *archive;
    (void)pd;
    if (!format || !xpak_parse(format, &context, NULL)) return false;
    archive = (xx_xpak *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = (uint64_t)context.unpacked_size;
    archive->declared_size = context.declared_size;
    archive->truncated = context.truncated;
    format->number_of_archive_records = 1U;
    format->format_size = context.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_xpak_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xpak_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_xpak_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xpak_handle_base_info(format, pd))
               ? ((xx_xpak *)format)->number_of_records : 0U;
}

bool xx_xpak_unpack_to_device(xx_xpak *archive, xx_io_device *destination,
                              xx_pd_struct *pd) {
    xpak_context context;
    if (!archive || !xpak_parse(&archive->format, &context, NULL)) return false;
    return xpak_unpack_context(&archive->format, &context, destination, pd);
}

xx_archive_record_state *xx_xpak_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xpak_stream *stream;
    xx_archive_record_state *state;
    xpak_context context;
    (void)pd;
    if (!xpak_parse(format, &context, NULL)) return NULL;
    stream = (xpak_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xpak_stream_free;
    state->total_records = 1U;
    if (!xpak_copy_options(&state->options, options) ||
        !xpak_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_xpak_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_xpak_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xpak_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (xpak_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_xpak_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xpak_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (xpak_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = xpak_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return xpak_unpack_context(format, &stream->context, NULL, pd);
    if (!xpak_safe_output_name(stream->context.name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->context.name)
               : xx_str_concat(base, stream->context.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = xpak_unpack_context(format, &stream->context, destination,
                                     pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_xpak_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
