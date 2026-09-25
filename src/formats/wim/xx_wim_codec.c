/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * WIM chunk decoders.
 *
 * XPRESS  LZ77+Huffman as published in [MS-XCA] 2.2: a 256-byte table of 512
 *         four-bit code lengths, then 16-bit little-endian words read most
 *         significant bit first, with long match lengths taken as whole
 *         bytes from the word stream's current position.  WIM chunks never
 *         exceed the format's 64 KiB block, so one table covers a chunk.
 * LZX     The LZX of [MS-PATCH] in its WIM framing: the window is the chunk
 *         size, a block header carries a one-bit "32768 bytes" flag before
 *         the explicit size, and E8 call translation is always on with a
 *         translation size of 12000000.
 * LZMS    No public specification exists.  This decoder was written from the
 *         format as 7-Zip's LzmsDecoder.cpp (LGPL; read for understanding,
 *         no code taken) and wimlib describe it: a range coder reading
 *         16-bit words forwards and a Huffman bit stream reading them
 *         backwards from the end of the same buffer, adaptive Huffman codes
 *         rebuilt from symbol frequencies, match-source queues whose update
 *         is delayed by one item, delta matches, and an x86 filter applied
 *         to the finished output.  The code-length construction must match
 *         the encoder's bit for bit; it is the in-place Huffman construction
 *         with the length-limit fix-up that 7-Zip's public-domain HuffEnc.c
 *         also uses.
 *
 * Input past the end of a chunk reads as zero bits, the way every reference
 * decoder treats it; output is always bounded by the caller's size, so a
 * hostile chunk can only fail or produce wrong bytes, which the reader then
 * rejects by SHA-1.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xx_wim_codec.h"

#include "xxfclib/memory/xx_memory.h"

static uint32_t wc_le16(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U);
}

static uint32_t wc_le32(const uint8_t *b) {
    return wc_le16(b) | (wc_le16(b + 2U) << 16U);
}

static void wc_put_le32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8U);
    b[2] = (uint8_t)(v >> 16U);
    b[3] = (uint8_t)(v >> 24U);
}

/* ---------------------------------------------------------------------- */
/* Canonical Huffman decoding                                              */

#define WH_FAST_BITS 10U
#define WH_MAX_LEN 16U
#define WH_MAX_SYMS 800U

/* Codes up to WH_FAST_BITS long resolve with one table lookup; longer ones
 * are found by comparing against each length's first canonical code. */
typedef struct wim_huff_s {
    uint16_t fast[1U << WH_FAST_BITS]; /* (symbol << 5) | length, 0 = slow */
    uint16_t count[WH_MAX_LEN + 1U];
    uint32_t first[WH_MAX_LEN + 1U];
    uint16_t index[WH_MAX_LEN + 1U];
    uint16_t sorted[WH_MAX_SYMS];
    unsigned max_len;
} wim_huff;

/* An over-subscribed code is refused.  An incomplete one is accepted: its
 * unused codes have no table entry and fail when met, never before. */
static bool wh_build(wim_huff *h, const uint8_t *lens, unsigned nsyms,
                     unsigned max_len) {
    uint16_t next[WH_MAX_LEN + 1U];
    unsigned len, sym, idx = 0U;
    int32_t left = 1;
    uint32_t code = 0U;
    if (!h || !lens || nsyms > WH_MAX_SYMS || max_len == 0U ||
        max_len > WH_MAX_LEN)
        return false;
    xx_rt_memset(h->count, 0, sizeof(h->count));
    for (sym = 0U; sym < nsyms; ++sym) {
        if (lens[sym] > max_len) return false;
        ++h->count[lens[sym]];
    }
    h->count[0] = 0U;
    for (len = 1U; len <= max_len; ++len) {
        left = left * 2 - (int32_t)h->count[len];
        if (left < 0) return false;
    }
    for (len = 1U; len <= max_len; ++len) {
        h->first[len] = code;
        h->index[len] = (uint16_t)idx;
        next[len] = (uint16_t)idx;
        idx += h->count[len];
        code = (code + h->count[len]) << 1U;
    }
    for (sym = 0U; sym < nsyms; ++sym)
        if (lens[sym] != 0U) h->sorted[next[lens[sym]]++] = (uint16_t)sym;
    xx_rt_memset(h->fast, 0, sizeof(h->fast));
    for (len = 1U; len <= max_len && len <= WH_FAST_BITS; ++len) {
        unsigned shift = WH_FAST_BITS - len, n;
        for (n = 0U; n < h->count[len]; ++n) {
            uint32_t start = (h->first[len] + n) << shift;
            uint32_t span = 1U << shift, k;
            uint16_t entry =
                (uint16_t)(((uint32_t)h->sorted[h->index[len] + n] << 5U) |
                           len);
            for (k = 0U; k < span; ++k) h->fast[start + k] = entry;
        }
    }
    h->max_len = max_len;
    return true;
}

/* @p top holds the next 32 stream bits, the first one in bit 31. */
static int wh_decode(const wim_huff *h, uint32_t top, unsigned *used) {
    uint16_t entry = h->fast[top >> (32U - WH_FAST_BITS)];
    unsigned len;
    if (entry != 0U) {
        *used = entry & 31U;
        return (int)(entry >> 5U);
    }
    for (len = WH_FAST_BITS + 1U; len <= h->max_len; ++len) {
        uint32_t delta = (top >> (32U - len)) - h->first[len];
        if (delta < h->count[len]) {
            *used = len;
            return (int)h->sorted[h->index[len] + delta];
        }
    }
    return -1;
}

/* ---------------------------------------------------------------------- */
/* XPRESS                                                                  */

static uint32_t xpress_word(const uint8_t *in, size_t size, size_t at) {
    return (at < size && size - at >= 2U) ? wc_le16(in + at) : 0U;
}

static bool xpress_decode(wim_huff *h, const uint8_t *in, size_t in_size,
                          uint8_t *out, size_t out_size) {
    uint8_t lens[512];
    uint32_t bits;
    int extra = 16;
    size_t cur = 256U, pos = 0U;
    unsigned i;
    if (in_size < 256U) return false;
    for (i = 0U; i < 256U; ++i) {
        lens[2U * i] = (uint8_t)(in[i] & 0x0FU);
        lens[2U * i + 1U] = (uint8_t)(in[i] >> 4U);
    }
    if (!wh_build(h, lens, 512U, 15U)) return false;
    bits = (xpress_word(in, in_size, cur) << 16U) |
           xpress_word(in, in_size, cur + 2U);
    cur += 4U;
    while (pos < out_size) {
        unsigned used, obits;
        uint64_t length;
        uint32_t offset;
        int sym = wh_decode(h, bits, &used);
        if (sym < 0) return false;
        bits <<= used;
        extra -= (int)used;
        if (extra < 0) {
            bits |= xpress_word(in, in_size, cur) << (unsigned)(-extra);
            extra += 16;
            cur += 2U;
        }
        if (sym < 256) {
            out[pos++] = (uint8_t)sym;
            continue;
        }
        sym -= 256;
        length = (uint64_t)(sym & 15);
        obits = (unsigned)sym >> 4U;
        if (length == 15U) {
            if (cur >= in_size) return false;
            length = in[cur++];
            if (length == 255U) {
                if (in_size - cur < 2U) return false;
                length = wc_le16(in + cur);
                cur += 2U;
                if (length == 0U) {
                    if (in_size - cur < 4U) return false;
                    length = wc_le32(in + cur);
                    cur += 4U;
                }
                if (length < 15U) return false;
                length -= 15U;
            }
            length += 15U;
        }
        length += 3U;
        offset = obits != 0U ? (bits >> (32U - obits)) : 0U;
        offset |= 1U << obits;
        if (obits != 0U) bits <<= obits;
        extra -= (int)obits;
        if (extra < 0) {
            bits |= xpress_word(in, in_size, cur) << (unsigned)(-extra);
            extra += 16;
            cur += 2U;
        }
        if ((size_t)offset > pos || length > (uint64_t)(out_size - pos))
            return false;
        {
            uint8_t *dst = out + pos;
            const uint8_t *src = dst - offset;
            size_t n = (size_t)length;
            while (n--) *dst++ = *src++;
        }
        pos += (size_t)length;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* LZX                                                                     */

#define LZX_MIN_ORDER 15U
#define LZX_MAX_ORDER 21U
#define LZX_MAX_SLOTS 50U
#define LZX_MAX_MAIN (256U + 8U * LZX_MAX_SLOTS)
#define LZX_LEN_SYMS 249U
#define LZX_PRE_SYMS 20U
#define LZX_ALIGNED_SYMS 8U
#define LZX_E8_SIZE 12000000

typedef struct lzx_state_s {
    wim_huff main, length, aligned, pre;
    uint8_t main_lens[LZX_MAX_MAIN];
    uint8_t length_lens[LZX_LEN_SYMS];
    uint32_t base[LZX_MAX_SLOTS];
    uint8_t extra[LZX_MAX_SLOTS];
} lzx_state;

typedef struct lzx_bits_s {
    const uint8_t *in;
    size_t size;
    size_t pos; /* bytes loaded into buf, counting zero padding */
    uint64_t buf;
    unsigned count;
} lzx_bits;

static void lzx_refill(lzx_bits *b) {
    while (b->count <= 48U) {
        uint32_t word = (b->pos < b->size && b->size - b->pos >= 2U)
                            ? wc_le16(b->in + b->pos) : 0U;
        b->pos += 2U;
        b->buf |= (uint64_t)word << (48U - b->count);
        b->count += 16U;
    }
}

static uint32_t lzx_peek32(lzx_bits *b) {
    lzx_refill(b);
    return (uint32_t)(b->buf >> 32U);
}

static void lzx_skip(lzx_bits *b, unsigned n) {
    b->buf <<= n;
    b->count -= n;
}

static uint32_t lzx_read(lzx_bits *b, unsigned n) {
    uint32_t value;
    if (n == 0U) return 0U;
    lzx_refill(b);
    value = (uint32_t)(b->buf >> (64U - n));
    lzx_skip(b, n);
    return value;
}

static int lzx_symbol(lzx_bits *b, const wim_huff *h) {
    unsigned used;
    int sym = wh_decode(h, lzx_peek32(b), &used);
    if (sym >= 0) lzx_skip(b, used);
    return sym;
}

/* Tree lengths [first, last) arrive as deltas against the previous block's
 * lengths, coded with a 20-symbol pre-tree sent just before them. */
static bool lzx_read_lengths(lzx_bits *b, wim_huff *pre, uint8_t *lens,
                             unsigned first, unsigned last) {
    uint8_t pre_lens[LZX_PRE_SYMS];
    unsigned i;
    for (i = 0U; i < LZX_PRE_SYMS; ++i) pre_lens[i] = (uint8_t)lzx_read(b, 4U);
    if (!wh_build(pre, pre_lens, LZX_PRE_SYMS, 15U)) return false;
    i = first;
    while (i < last) {
        int z = lzx_symbol(b, pre);
        unsigned run;
        if (z < 0) return false;
        if (z <= 16) {
            lens[i] = (uint8_t)((lens[i] + 17U - (unsigned)z) % 17U);
            ++i;
        } else if (z == 17 || z == 18) {
            run = z == 17 ? 4U + lzx_read(b, 4U) : 20U + lzx_read(b, 5U);
            if (run > last - i) return false;
            while (run--) lens[i++] = 0U;
        } else {
            int z2;
            uint8_t value;
            run = 4U + lzx_read(b, 1U);
            if (run > last - i) return false;
            z2 = lzx_symbol(b, pre);
            if (z2 < 0 || z2 > 16) return false;
            value = (uint8_t)((lens[i] + 17U - (unsigned)z2) % 17U);
            while (run--) lens[i++] = value;
        }
    }
    return true;
}

static void lzx_undo_e8(uint8_t *data, size_t size) {
    size_t i = 0U;
    if (size <= 10U) return;
    while (i < size - 10U) {
        if (data[i] != 0xE8U) {
            ++i;
            continue;
        }
        {
            int32_t abs_offset = (int32_t)wc_le32(data + i + 1U);
            int32_t pos = (int32_t)i;
            if (abs_offset >= 0) {
                if (abs_offset < LZX_E8_SIZE)
                    wc_put_le32(data + i + 1U, (uint32_t)(abs_offset - pos));
            } else if (abs_offset >= -pos) {
                wc_put_le32(data + i + 1U,
                            (uint32_t)(abs_offset + LZX_E8_SIZE));
            }
        }
        i += 5U;
    }
}

static void lzx_init_tables(lzx_state *x) {
    unsigned slot;
    uint32_t base = 0U;
    for (slot = 0U; slot < LZX_MAX_SLOTS; ++slot) {
        unsigned extra = slot < 4U ? 0U : (slot - 2U) >> 1U;
        if (extra > 17U) extra = 17U;
        x->extra[slot] = (uint8_t)extra;
        x->base[slot] = base;
        base += 1U << extra;
    }
}

static bool lzx_decode(lzx_state *x, const uint8_t *in, size_t in_size,
                       uint8_t *out, size_t out_size, uint32_t window) {
    static const uint8_t slots_for_order[LZX_MAX_ORDER + 1U - LZX_MIN_ORDER] =
        {30U, 32U, 34U, 36U, 38U, 42U, 50U};
    lzx_bits b;
    unsigned order = 0U, slots, main_syms;
    uint32_t r0 = 1U, r1 = 1U, r2 = 1U;
    size_t pos = 0U, target = 0U;
    while (order < 32U && (1UL << order) < (unsigned long)window) ++order;
    if (order < LZX_MIN_ORDER || order > LZX_MAX_ORDER ||
        (1UL << order) != (unsigned long)window || out_size > window)
        return false;
    slots = slots_for_order[order - LZX_MIN_ORDER];
    main_syms = 256U + 8U * slots;
    xx_rt_memset(x->main_lens, 0, sizeof(x->main_lens));
    xx_rt_memset(x->length_lens, 0, sizeof(x->length_lens));
    b.in = in;
    b.size = in_size;
    b.pos = 0U;
    b.buf = 0U;
    b.count = 0U;
    while (pos < out_size) {
        unsigned type = lzx_read(&b, 3U);
        uint32_t block;
        size_t block_start = target;
        if (lzx_read(&b, 1U)) {
            block = 32768U;
        } else {
            block = lzx_read(&b, 16U);
            if (order >= 16U) block = (block << 8U) | lzx_read(&b, 8U);
        }
        if (block == 0U) return false;
        /* A match may run past its block's end; the overrun counts against
         * the next block, so block ends are tracked cumulatively. */
        target = (size_t)block > out_size - target ? out_size
                                                   : target + (size_t)block;
        if (type == 1U || type == 2U) {
            if (type == 2U) {
                uint8_t aligned_lens[LZX_ALIGNED_SYMS];
                unsigned i;
                for (i = 0U; i < LZX_ALIGNED_SYMS; ++i)
                    aligned_lens[i] = (uint8_t)lzx_read(&b, 3U);
                if (!wh_build(&x->aligned, aligned_lens, LZX_ALIGNED_SYMS, 7U))
                    return false;
            }
            if (!lzx_read_lengths(&b, &x->pre, x->main_lens, 0U, 256U) ||
                !lzx_read_lengths(&b, &x->pre, x->main_lens, 256U,
                                  main_syms) ||
                !wh_build(&x->main, x->main_lens, main_syms, 16U) ||
                !lzx_read_lengths(&b, &x->pre, x->length_lens, 0U,
                                  LZX_LEN_SYMS) ||
                !wh_build(&x->length, x->length_lens, LZX_LEN_SYMS, 16U))
                return false;
            while (pos < target) {
                int sym = lzx_symbol(&b, &x->main);
                unsigned header, slot;
                uint32_t length, offset;
                if (sym < 0) return false;
                if (sym < 256) {
                    out[pos++] = (uint8_t)sym;
                    continue;
                }
                sym -= 256;
                header = (unsigned)sym & 7U;
                slot = (unsigned)sym >> 3U;
                length = header + 2U;
                if (header == 7U) {
                    int more = lzx_symbol(&b, &x->length);
                    if (more < 0) return false;
                    length += (uint32_t)more;
                }
                if (slot == 0U) {
                    offset = r0;
                } else if (slot == 1U) {
                    offset = r1;
                    r1 = r0;
                    r0 = offset;
                } else if (slot == 2U) {
                    offset = r2;
                    r2 = r0;
                    r0 = offset;
                } else {
                    unsigned extra = x->extra[slot];
                    uint32_t value;
                    if (type == 2U && extra >= 3U) {
                        int low;
                        value = lzx_read(&b, extra - 3U) << 3U;
                        low = lzx_symbol(&b, &x->aligned);
                        if (low < 0) return false;
                        value += (uint32_t)low;
                    } else {
                        value = lzx_read(&b, extra);
                    }
                    offset = x->base[slot] + value - 2U;
                    r2 = r1;
                    r1 = r0;
                    r0 = offset;
                }
                if (offset == 0U || (size_t)offset > pos ||
                    (size_t)length > out_size - pos)
                    return false;
                {
                    uint8_t *dst = out + pos;
                    const uint8_t *src = dst - offset;
                    uint32_t n = length;
                    while (n--) *dst++ = *src++;
                }
                pos += length;
            }
        } else if (type == 3U) {
            /* The raw block starts on the next 16-bit boundary, and a
             * stream that is already on one skips a whole padding word. */
            size_t consumed = b.pos * 8U - b.count;
            size_t start = (consumed % 16U) != 0U ? (consumed + 15U) / 16U * 2U
                                                 : consumed / 8U + 2U;
            size_t amount;
            if (pos != block_start || start > in_size || in_size - start < 12U)
                return false;
            amount = target - pos;
            r0 = wc_le32(in + start);
            r1 = wc_le32(in + start + 4U);
            r2 = wc_le32(in + start + 8U);
            start += 12U;
            if (r0 == 0U || r1 == 0U || r2 == 0U || amount > in_size - start)
                return false;
            xx_rt_memcpy(out + pos, in + start, amount);
            pos += amount;
            start += amount;
            if ((block & 1U) != 0U) ++start;
            b.pos = start;
            b.buf = 0U;
            b.count = 0U;
        } else {
            return false;
        }
    }
    lzx_undo_e8(out, out_size);
    return true;
}

/* ---------------------------------------------------------------------- */
/* LZMS                                                                    */

#define LZMS_LIT_SYMS 256U
#define LZMS_LEN_SYMS 54U
#define LZMS_OFF_SYMS 799U
#define LZMS_POWER_SYMS 8U
#define LZMS_MAX_CODE_LEN 15U
#define LZMS_X86_WINDOW 65535
#define LZMS_X86_REACH 1023

typedef struct lzms_prob_s {
    uint32_t zeros;
    uint64_t history;
} lzms_prob;

typedef struct lzms_code_s {
    uint32_t freq[WH_MAX_SYMS];
    uint8_t lens[WH_MAX_SYMS];
    unsigned nsyms;
    unsigned period;
    unsigned remaining;
    wim_huff huff;
} lzms_code;

typedef struct lzms_state_s {
    lzms_prob main_probs[16];
    lzms_prob match_probs[32];
    lzms_prob lz_probs[3][64];
    lzms_prob delta_probs[3][64];
    lzms_code literal, lz_offset, length, delta_offset, delta_power;
    uint32_t off_base[LZMS_OFF_SYMS];
    uint8_t off_extra[LZMS_OFF_SYMS];
    uint32_t len_base[LZMS_LEN_SYMS];
    uint8_t len_extra[LZMS_LEN_SYMS];
    /* code construction scratch */
    uint32_t key[WH_MAX_SYMS];
    uint32_t node_freq[WH_MAX_SYMS];
    uint16_t node_parent[WH_MAX_SYMS];
    uint16_t node_depth[WH_MAX_SYMS];
    int32_t x86_last[65536];
} lzms_state;

typedef struct lzms_rc_s {
    const uint8_t *in;
    size_t words;
    size_t next;
    uint32_t range;
    uint32_t code;
} lzms_rc;

typedef struct lzms_bits_s {
    const uint8_t *in;
    size_t next; /* words not yet loaded, counted from the start */
    uint64_t buf;
    unsigned count;
} lzms_bits;

/* How many offset slots carry each number of extra bits (index = bits). */
static const uint8_t lzms_off_runs[31] = {
    8U,  0U,  9U,  7U,  10U, 15U, 15U, 20U, 20U, 30U, 33U,
    40U, 42U, 45U, 60U, 73U, 80U, 85U, 95U, 105U, 6U, 0U,
    0U,  0U,  0U,  0U,  0U,  0U,  0U,  0U,  1U};

static const uint8_t lzms_len_bits[LZMS_LEN_SYMS] = {
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,  0U,  0U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 1U, 1U, 1U, 2U,  2U,  2U, 2U, 2U, 2U,
    3U, 3U, 3U, 3U, 4U, 4U, 4U, 4U, 4U, 5U, 5U, 6U, 7U,  8U,  9U, 10U, 16U,
    30U};

static void lzms_init_tables(lzms_state *s) {
    unsigned bits, slot = 0U, i;
    uint32_t base = 1U;
    for (bits = 0U; bits < sizeof(lzms_off_runs); ++bits) {
        unsigned n;
        for (n = 0U; n < lzms_off_runs[bits] && slot < LZMS_OFF_SYMS; ++n) {
            s->off_extra[slot] = (uint8_t)bits;
            s->off_base[slot] = base;
            base += 1U << bits;
            ++slot;
        }
    }
    base = 1U;
    for (i = 0U; i < LZMS_LEN_SYMS; ++i) {
        s->len_extra[i] = lzms_len_bits[i];
        s->len_base[i] = base;
        base += 1U << lzms_len_bits[i];
    }
}

static uint32_t lzms_rc_word(lzms_rc *rc) {
    uint32_t word = rc->next < rc->words ? wc_le16(rc->in + rc->next * 2U) : 0U;
    ++rc->next;
    return word;
}

static unsigned lzms_bit(lzms_rc *rc, lzms_prob *probs, uint32_t *state,
                         uint32_t states) {
    lzms_prob *p = &probs[*state];
    uint32_t prob = p->zeros, bound;
    unsigned bit;
    if (prob == 0U)
        prob = 1U;
    else if (prob >= 64U)
        prob = 63U;
    if (rc->range <= 0xFFFFU) {
        rc->range <<= 16U;
        rc->code = (rc->code << 16U) | lzms_rc_word(rc);
    }
    bound = (rc->range >> 6U) * prob;
    if (rc->code < bound) {
        rc->range = bound;
        bit = 0U;
    } else {
        rc->range -= bound;
        rc->code -= bound;
        bit = 1U;
    }
    p->zeros = (uint32_t)((int32_t)p->zeros + (int32_t)(p->history >> 63U) -
                          (int32_t)bit);
    p->history = (p->history << 1U) | bit;
    *state = ((*state << 1U) | bit) & (states - 1U);
    return bit;
}

static void lzms_refill(lzms_bits *b) {
    while (b->count <= 48U) {
        uint32_t word = 0U;
        if (b->next != 0U) {
            --b->next;
            word = wc_le16(b->in + b->next * 2U);
        }
        b->buf |= (uint64_t)word << (48U - b->count);
        b->count += 16U;
    }
}

static uint32_t lzms_read(lzms_bits *b, unsigned n) {
    uint32_t value;
    if (n == 0U) return 0U;
    lzms_refill(b);
    value = (uint32_t)(b->buf >> (64U - n));
    b->buf <<= n;
    b->count -= n;
    return value;
}

static void lzms_heap_sift(uint32_t *a, size_t root, size_t n) {
    for (;;) {
        size_t child = root * 2U + 1U;
        uint32_t t;
        if (child >= n) return;
        if (child + 1U < n && a[child + 1U] > a[child]) ++child;
        if (a[root] >= a[child]) return;
        t = a[root];
        a[root] = a[child];
        a[child] = t;
        root = child;
    }
}

static void lzms_sort(uint32_t *a, size_t n) {
    size_t i;
    if (n < 2U) return;
    for (i = n / 2U; i-- > 0U;) lzms_heap_sift(a, i, n);
    for (i = n - 1U; i > 0U; --i) {
        uint32_t t = a[0];
        a[0] = a[i];
        a[i] = t;
        lzms_heap_sift(a, 0U, i);
    }
}

/* Code lengths from frequencies (n >= 2, every frequency >= 1).  Leaves are
 * taken in (frequency, symbol) order and a leaf wins a frequency tie against
 * an internal node; depths past the limit are folded back onto the deepest
 * level that still has room; lengths are then dealt out longest-first in
 * that same order. */
static void lzms_make_lengths(lzms_state *s, lzms_code *c) {
    uint32_t count[WH_MAX_LEN + 2U];
    unsigned n = c->nsyms, i = 0U, b = 0U, e = 0U, len;
    for (i = 0U; i < n; ++i) s->key[i] = (c->freq[i] << 10U) | i;
    lzms_sort(s->key, n);
    i = 0U;
    while (e < n - 1U) {
        uint32_t sum = 0U;
        unsigned k;
        for (k = 0U; k < 2U; ++k) {
            if (i < n && (b == e || (s->key[i] >> 10U) <= s->node_freq[b])) {
                sum += s->key[i] >> 10U;
                ++i;
            } else {
                sum += s->node_freq[b];
                s->node_parent[b] = (uint16_t)e;
                ++b;
            }
        }
        s->node_freq[e++] = sum;
    }
    xx_rt_memset(count, 0, sizeof(count));
    count[1] = 2U;
    s->node_depth[n - 2U] = 0U;
    for (e = n - 2U; e-- > 0U;) {
        unsigned depth = (unsigned)s->node_depth[s->node_parent[e]] + 1U;
        s->node_depth[e] = (uint16_t)depth;
        len = depth;
        if (len >= LZMS_MAX_CODE_LEN) {
            len = LZMS_MAX_CODE_LEN - 1U;
            while (len > 1U && count[len] == 0U) --len;
        }
        --count[len];
        count[len + 1U] += 2U;
    }
    i = 0U;
    for (len = LZMS_MAX_CODE_LEN; len >= 1U; --len) {
        uint32_t k;
        for (k = count[len]; k != 0U && i < n; --k)
            c->lens[s->key[i++] & 1023U] = (uint8_t)len;
    }
}

static bool lzms_code_build(lzms_state *s, lzms_code *c) {
    lzms_make_lengths(s, c);
    return wh_build(&c->huff, c->lens, c->nsyms, LZMS_MAX_CODE_LEN);
}

static bool lzms_code_init(lzms_state *s, lzms_code *c, unsigned nsyms,
                           unsigned period) {
    unsigned i;
    c->nsyms = nsyms;
    c->period = period;
    c->remaining = period;
    for (i = 0U; i < nsyms; ++i) c->freq[i] = 1U;
    return lzms_code_build(s, c);
}

static int lzms_symbol(lzms_state *s, lzms_code *c, lzms_bits *b) {
    unsigned used;
    int sym;
    lzms_refill(b);
    sym = wh_decode(&c->huff, (uint32_t)(b->buf >> 32U), &used);
    if (sym < 0 || (unsigned)sym >= c->nsyms) return -1;
    b->buf <<= used;
    b->count -= used;
    ++c->freq[sym];
    if (--c->remaining == 0U) {
        unsigned i;
        if (!lzms_code_build(s, c)) return -1;
        c->remaining = c->period;
        for (i = 0U; i < c->nsyms; ++i) c->freq[i] = (c->freq[i] >> 1U) + 1U;
    }
    return sym;
}

static void lzms_prob_init(lzms_prob *p, size_t n) {
    size_t i;
    for (i = 0U; i < n; ++i) {
        p[i].zeros = 48U;
        p[i].history = 0x55555555U;
    }
}

/* Undo the encoder's x86 filter.  Opcode candidates are scanned from offset
 * 1 up to 16 bytes before the end; a displacement is translated back to
 * relative form only while the scan is "in code", which it is for a
 * while after two references to the same 16-bit target land within
 * 64 KiB of each other. */
static void lzms_undo_x86(int32_t *last_use, uint8_t *data, size_t size) {
    int32_t last_x86 = -LZMS_X86_REACH - 1;
    int32_t i = 0, limit;
    size_t k;
    if (size <= 17U || size > 0x7FFFFFF0U) return;
    for (k = 0U; k < 65536U; ++k) last_use[k] = -LZMS_X86_WINDOW - 1;
    limit = (int32_t)size - 16;
    for (;;) {
        int32_t reach = LZMS_X86_REACH;
        unsigned span;
        uint8_t op;
        uint32_t value, target;
        do {
            ++i;
        } while (i < limit && data[i] != 0x48U && data[i] != 0x4CU &&
                 data[i] != 0xE8U && data[i] != 0xE9U && data[i] != 0xF0U &&
                 data[i] != 0xFFU);
        if (i >= limit) break;
        op = data[i];
        if (op == 0x48U || op == 0x4CU) {
            if ((data[i + 2] & 7U) != 5U) continue;
            if (data[i + 1] != 0x8DU &&
                (data[i + 1] != 0x8BU || op != 0x48U ||
                 (data[i + 2] != 0x05U && data[i + 2] != 0x0DU)))
                continue;
            span = 3U;
        } else if (op == 0xE8U) {
            span = 1U;
            reach /= 2;
        } else if (op == 0xE9U) {
            i += 4;
            continue;
        } else if (op == 0xF0U) {
            if (data[i + 1] != 0x83U || data[i + 2] != 0x05U) continue;
            span = 3U;
        } else {
            if (data[i + 1] != 0x15U) continue;
            span = 2U;
        }
        value = wc_le32(data + i + span);
        if (i - last_x86 <= reach) {
            value -= (uint32_t)i;
            wc_put_le32(data + i + span, value);
        }
        target = ((uint32_t)i + value) & 0xFFFFU;
        i += (int32_t)span + 3;
        if (i - last_use[target] <= LZMS_X86_WINDOW) last_x86 = i;
        last_use[target] = i;
    }
}

static bool lzms_decode(lzms_state *s, const uint8_t *in, size_t in_size,
                        uint8_t *out, size_t out_size) {
    lzms_rc rc;
    lzms_bits bits;
    uint32_t reps[4];
    uint64_t delta_reps[4];
    uint32_t main_state = 0U, match_state = 0U;
    uint32_t lz_state[3] = {0U, 0U, 0U}, delta_state[3] = {0U, 0U, 0U};
    unsigned prev = 0U, i, off_syms = 0U;
    size_t pos = 0U;
    if (in_size < 4U || (in_size & 1U) != 0U) return false;
    rc.in = in;
    rc.words = in_size / 2U;
    rc.next = 2U;
    rc.range = 0xFFFFFFFFU;
    rc.code = (wc_le16(in) << 16U) | wc_le16(in + 2U);
    if (rc.code >= rc.range) return false;
    bits.in = in;
    bits.next = in_size / 2U;
    bits.buf = 0U;
    bits.count = 0U;
    for (i = 0U; i < 4U; ++i) {
        reps[i] = i + 1U;
        delta_reps[i] = i + 1U;
    }
    lzms_prob_init(s->main_probs, 16U);
    lzms_prob_init(s->match_probs, 32U);
    lzms_prob_init(&s->lz_probs[0][0], 3U * 64U);
    lzms_prob_init(&s->delta_probs[0][0], 3U * 64U);
    if (out_size >= 2U) {
        size_t last = out_size - 1U;
        while (off_syms < LZMS_OFF_SYMS && s->off_base[off_syms] <= last)
            ++off_syms;
    }
    if (off_syms < 2U) off_syms = 2U;
    if (!lzms_code_init(s, &s->literal, LZMS_LIT_SYMS, 1024U) ||
        !lzms_code_init(s, &s->lz_offset, off_syms, 1024U) ||
        !lzms_code_init(s, &s->length, LZMS_LEN_SYMS, 512U) ||
        !lzms_code_init(s, &s->delta_offset, off_syms, 1024U) ||
        !lzms_code_init(s, &s->delta_power, LZMS_POWER_SYMS, 512U))
        return false;
    while (pos < out_size) {
        if (lzms_bit(&rc, s->main_probs, &main_state, 16U) == 0U) {
            int lit = lzms_symbol(s, &s->literal, &bits);
            if (lit < 0) return false;
            out[pos++] = (uint8_t)lit;
            prev = 0U;
        } else if (lzms_bit(&rc, s->match_probs, &match_state, 32U) == 0U) {
            uint32_t dist, length;
            int slot;
            if (lzms_bit(&rc, s->lz_probs[0], &lz_state[0], 64U) == 0U) {
                slot = lzms_symbol(s, &s->lz_offset, &bits);
                if (slot < 0) return false;
                dist = s->off_base[slot] +
                       lzms_read(&bits, s->off_extra[slot]);
                reps[3] = reps[2];
                reps[2] = reps[1];
                reps[1] = reps[0];
                reps[0] = dist;
            } else {
                unsigned idx, k;
                if (lzms_bit(&rc, s->lz_probs[1], &lz_state[1], 64U) == 0U)
                    idx = 0U;
                else if (lzms_bit(&rc, s->lz_probs[2], &lz_state[2], 64U) ==
                         0U)
                    idx = 1U;
                else
                    idx = 2U;
                idx += prev == 1U ? 1U : 0U;
                dist = reps[idx];
                for (k = idx; k > 0U; --k) reps[k] = reps[k - 1U];
                reps[0] = dist;
            }
            slot = lzms_symbol(s, &s->length, &bits);
            if (slot < 0) return false;
            length = s->len_base[slot] + lzms_read(&bits, s->len_extra[slot]);
            if (dist == 0U || (size_t)dist > pos ||
                (size_t)length > out_size - pos)
                return false;
            {
                uint8_t *dst = out + pos;
                const uint8_t *src = dst - dist;
                uint32_t n = length;
                while (n--) *dst++ = *src++;
            }
            pos += length;
            prev = 1U;
        } else {
            uint64_t pair;
            uint32_t raw, power, length, dist;
            size_t span;
            int slot;
            if (lzms_bit(&rc, s->delta_probs[0], &delta_state[0], 64U) == 0U) {
                int p = lzms_symbol(s, &s->delta_power, &bits);
                if (p < 0) return false;
                slot = lzms_symbol(s, &s->delta_offset, &bits);
                if (slot < 0) return false;
                raw = s->off_base[slot] + lzms_read(&bits, s->off_extra[slot]);
                pair = ((uint64_t)(uint32_t)p << 32U) | raw;
                delta_reps[3] = delta_reps[2];
                delta_reps[2] = delta_reps[1];
                delta_reps[1] = delta_reps[0];
                delta_reps[0] = pair;
            } else {
                unsigned idx, k;
                if (lzms_bit(&rc, s->delta_probs[1], &delta_state[1], 64U) ==
                    0U)
                    idx = 0U;
                else if (lzms_bit(&rc, s->delta_probs[2], &delta_state[2],
                                  64U) == 0U)
                    idx = 1U;
                else
                    idx = 2U;
                idx += prev == 2U ? 1U : 0U;
                pair = delta_reps[idx];
                for (k = idx; k > 0U; --k) delta_reps[k] = delta_reps[k - 1U];
                delta_reps[0] = pair;
            }
            power = (uint32_t)(pair >> 32U);
            raw = (uint32_t)pair;
            slot = lzms_symbol(s, &s->length, &bits);
            if (slot < 0) return false;
            length = s->len_base[slot] + lzms_read(&bits, s->len_extra[slot]);
            if (power >= 32U || raw > (0xFFFFFFFFU >> power)) return false;
            dist = raw << power;
            span = (size_t)1U << power;
            if ((uint64_t)dist + span > (uint64_t)pos ||
                (size_t)length > out_size - pos)
                return false;
            {
                uint8_t *dst = out + pos;
                uint32_t n = length;
                while (n--) {
                    *dst = (uint8_t)(dst[-(ptrdiff_t)span] + dst[-(ptrdiff_t)dist] -
                                     dst[-(ptrdiff_t)(dist + span)]);
                    ++dst;
                }
            }
            pos += length;
            prev = 2U;
        }
    }
    lzms_undo_x86(s->x86_last, out, out_size);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Dispatch                                                                */

struct xx_wim_codec {
    unsigned method;
    wim_huff *xpress;
    lzx_state *lzx;
    lzms_state *lzms;
};

xx_wim_codec *xx_wim_codec_create(unsigned method) {
    xx_wim_codec *codec;
    if (method != XX_WIM_CODEC_XPRESS && method != XX_WIM_CODEC_LZX &&
        method != XX_WIM_CODEC_LZMS)
        return NULL;
    codec = (xx_wim_codec *)xx_mem_calloc(1U, sizeof(*codec));
    if (!codec) return NULL;
    codec->method = method;
    if (method == XX_WIM_CODEC_XPRESS) {
        codec->xpress = (wim_huff *)xx_mem_calloc(1U, sizeof(wim_huff));
        if (!codec->xpress) goto fail;
    } else if (method == XX_WIM_CODEC_LZX) {
        codec->lzx = (lzx_state *)xx_mem_calloc(1U, sizeof(lzx_state));
        if (!codec->lzx) goto fail;
        lzx_init_tables(codec->lzx);
    } else {
        codec->lzms = (lzms_state *)xx_mem_calloc(1U, sizeof(lzms_state));
        if (!codec->lzms) goto fail;
        lzms_init_tables(codec->lzms);
    }
    return codec;
fail:
    xx_wim_codec_free(codec);
    return NULL;
}

void xx_wim_codec_free(xx_wim_codec *codec) {
    if (!codec) return;
    if (codec->xpress) xx_mem_free(codec->xpress);
    if (codec->lzx) xx_mem_free(codec->lzx);
    if (codec->lzms) xx_mem_free(codec->lzms);
    xx_mem_free(codec);
}

bool xx_wim_codec_decode(xx_wim_codec *codec, const uint8_t *in,
                         size_t in_size, uint8_t *out, size_t out_size,
                         uint32_t window_size) {
    if (!codec || !in || !out || out_size == 0U ||
        out_size > XX_WIM_CODEC_MAX_CHUNK)
        return false;
    switch (codec->method) {
    case XX_WIM_CODEC_XPRESS:
        return xpress_decode(codec->xpress, in, in_size, out, out_size);
    case XX_WIM_CODEC_LZX:
        return lzx_decode(codec->lzx, in, in_size, out, out_size,
                          window_size);
    case XX_WIM_CODEC_LZMS:
        return lzms_decode(codec->lzms, in, in_size, out, out_size);
    default:
        return false;
    }
}
