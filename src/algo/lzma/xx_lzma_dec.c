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

/* LZMA / LZMA2 decompressor — clean C implementation.
 *
 * Based on the LZMA algorithm by Igor Pavlov (public domain LZMA SDK).
 * This is an independent clean-room reimplementation using the same
 * range-coder arithmetic and probability model, rewritten under the
 * xxfclib MIT licence.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_lzma_internal.h"
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Range decoder
 * ========================================================================= */

static bool rd_refill(lzma_range_dec *rd)
{
    if (rd->error || rd->eof) return false;
    if (rd->ibuf_pos < rd->ibuf_len) return true;

    size_t want = sizeof(rd->ibuf);
    if (rd->remaining >= 0) {
        if (rd->remaining == 0) { rd->eof = true; return false; }
        if ((int64_t)want > rd->remaining) want = (size_t)rd->remaining;
    }

    ssize_t got;
    if (rd->dev) {
        got = xx_io_read(rd->dev, rd->ibuf, want);
    } else {
        size_t avail = rd->mem_size - rd->mem_pos;
        got = avail == 0 ? 0 : (ssize_t)(avail < want ? avail : want);
        if (got > 0) { xx_rt_memcpy(rd->ibuf, rd->mem + rd->mem_pos, (size_t)got); rd->mem_pos += (size_t)got; }
    }
    if (got <= 0) { rd->eof = true; return false; }
    if (rd->remaining >= 0) rd->remaining -= got;
    rd->ibuf_pos = 0;
    rd->ibuf_len = (size_t)got;
    return true;
}

static uint8_t rd_byte(lzma_range_dec *rd)
{
    if (!rd_refill(rd)) { rd->error = true; return 0; }
    return rd->ibuf[rd->ibuf_pos++];
}

static uint64_t rd_bytes_left(const lzma_range_dec *rd)
{
    uint64_t buffered = (uint64_t)(rd->ibuf_len - rd->ibuf_pos);
    return buffered + (rd->remaining > 0 ? (uint64_t)rd->remaining : 0u);
}

static bool rd_read_exact(lzma_range_dec *rd, uint8_t *data, size_t size,
                          xx_pd_struct *pd)
{
    for (size_t i = 0; i < size; i++) {
        if (pd && (i & 0xFFFu) == 0 && xx_pd_is_stopped(pd)) return false;
        data[i] = rd_byte(rd);
        if (rd->error) return false;
    }
    return true;
}

bool lzma_rd_init(lzma_range_dec *rd, xx_io_device *dev,
                  const uint8_t *mem, size_t mem_size, int64_t remaining)
{
    xx_rt_memset(rd, 0, sizeof(*rd));
    rd->dev       = dev;
    rd->mem       = mem;
    rd->mem_size  = mem_size;
    rd->remaining = remaining;
    rd->range     = 0xFFFFFFFFu;
    if ((!dev && !mem) || remaining < 5 ||
        (mem && (uint64_t)remaining > (uint64_t)mem_size)) return false;
    /* A raw LZMA range stream starts with a zero byte. */
    if (rd_byte(rd) != 0 || rd->error) return false;
    rd->code  = ((uint32_t)rd_byte(rd) << 24) |
                ((uint32_t)rd_byte(rd) << 16) |
                ((uint32_t)rd_byte(rd) << 8)  |
                 (uint32_t)rd_byte(rd);
    return !rd->error;
}

void lzma_rd_free(lzma_range_dec *rd) { (void)rd; }

static void rd_normalise(lzma_range_dec *rd)
{
    if (rd->range < RC_TOP_VALUE) {
        rd->range <<= 8;
        rd->code   = (rd->code << 8) | rd_byte(rd);
    }
}

static int rd_bit(lzma_range_dec *rd, lzma_prob *prob)
{
    uint32_t bound = (rd->range >> RC_BIT_MODEL_TOTAL_BITS) * (*prob);
    int bit;
    if (rd->code < bound) {
        *prob += (RC_BIT_MODEL_TOTAL - *prob) >> RC_MOVE_BITS;
        rd->range = bound;
        bit = 0;
    } else {
        *prob -= *prob >> RC_MOVE_BITS;
        rd->code  -= bound;
        rd->range -= bound;
        bit = 1;
    }
    rd_normalise(rd);
    return bit;
}

static uint32_t rd_bittree(lzma_range_dec *rd, lzma_prob *probs, int n_bits)
{
    uint32_t m = 1;
    for (int i = 0; i < n_bits; i++)
        m = (m << 1) | rd_bit(rd, probs + m);
    return m - (1u << n_bits);
}

static uint32_t rd_bittree_rev(lzma_range_dec *rd, lzma_prob *probs, int n_bits)
{
    uint32_t m = 1, sym = 0;
    for (int i = 0; i < n_bits; i++) {
        int bit = rd_bit(rd, probs + m);
        m = (m << 1) | bit;
        sym |= (uint32_t)bit << i;
    }
    return sym;
}

static uint32_t rd_direct(lzma_range_dec *rd, int n_bits)
{
    uint32_t result = 0;
    for (int i = n_bits - 1; i >= 0; i--) {
        rd->range >>= 1;
        rd->code  -= rd->range;
        uint32_t t = (uint32_t)(-(int32_t)(rd->code >> 31));
        rd->code += rd->range & t;
        result |= ((t + 1) & 1u) << i;
        rd_normalise(rd);
    }
    return result;
}

/* =========================================================================
 * LZMA properties
 * ========================================================================= */

bool lzma_parse_props(const uint8_t *props, size_t props_size, lzma_props *out)
{
    if (!props || props_size < XX_LZMA_PROPS_SIZE) return false;
    int d = props[0];
    if (d >= 9 * 5 * 5) return false;
    out->pb = d / (9 * 5);
    d      -= out->pb * 9 * 5;
    out->lp = d / 9;
    out->lc = d - out->lp * 9;
    if (out->lc + out->lp > 4) return false;
    uint32_t ds = (uint32_t)props[1]        |
                  ((uint32_t)props[2] << 8)  |
                  ((uint32_t)props[3] << 16) |
                  ((uint32_t)props[4] << 24);
    if (ds > XX_LZMA_MAX_DICT_SIZE) return false;
    /* Round up to power of 2 (minimum 4096) */
    uint32_t real_ds = 4096;
    while (real_ds < ds) real_ds <<= 1;
    out->dict_size = real_ds;
    return true;
}

/* =========================================================================
 * Decoder state lifecycle
 * ========================================================================= */

static void probs_init(lzma_prob *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = PROB_INIT_VAL;
}

static void lzma_dec_reset_model(lzma_decoder *dec)
{
    size_t lit_count = (size_t)0x300 << (dec->props.lc + dec->props.lp);
    probs_init(dec->lit_probs, lit_count);
    for (int s = 0; s < LZMA_NUM_STATES; s++)
        for (int i = 0; i < (1 << LZMA_NUM_POS_BITS_MAX); i++) {
            dec->is_match[s][i] = PROB_INIT_VAL;
            dec->is_rep0_long[s][i] = PROB_INIT_VAL;
        }
    probs_init(dec->is_rep, LZMA_NUM_STATES);
    probs_init(dec->is_rep_g0, LZMA_NUM_STATES);
    probs_init(dec->is_rep_g1, LZMA_NUM_STATES);
    probs_init(dec->is_rep_g2, LZMA_NUM_STATES);
    for (int i = 0; i < LZMA_NUM_LEN_TO_POS_STATES; i++)
        probs_init(dec->pos_slot[i], 1 << 6);
    probs_init(dec->pos_decoders, LZMA_FULL_DISTANCES - LZMA_NUM_LEN_TO_POS_STATES * 4);
    probs_init(dec->pos_align, 1 << LZMA_NUM_ALIGN_BITS);
    for (int which = 0; which < 2; which++) {
        dec->len_choice[which] = PROB_INIT_VAL;
        dec->len_choice2[which] = PROB_INIT_VAL;
        for (int pb = 0; pb < (1 << LZMA_NUM_POS_BITS_MAX); pb++) {
            probs_init(dec->len_low[which][pb], 8);
            probs_init(dec->len_mid[which][pb], 8);
        }
        probs_init(dec->len_high[which], 256);
    }
    dec->state = 0;
    dec->rep[0] = dec->rep[1] = dec->rep[2] = dec->rep[3] = 1;
}

static void lzma_dec_reset_dictionary(lzma_decoder *dec)
{
    dec->dict_pos = 0;
    dec->dict_filled = 0;
}

static bool lzma_dec_set_lclppb(lzma_decoder *dec, int lc, int lp, int pb)
{
    if (lc < 0 || lp < 0 || pb < 0 || lc > 8 || lp > 4 || pb > 4 || lc + lp > 4)
        return false;
    if (dec->props.lc != lc || dec->props.lp != lp) {
        size_t count = (size_t)0x300 << (lc + lp);
        lzma_prob *replacement = (lzma_prob *)xx_mem_alloc(count * sizeof(lzma_prob));
        if (!replacement) return false;
        xx_mem_free(dec->lit_probs);
        dec->lit_probs = replacement;
    }
    dec->props.lc = lc;
    dec->props.lp = lp;
    dec->props.pb = pb;
    return true;
}

static void lzma_dec_note_output(lzma_decoder *dec)
{
    if (dec->dict_filled < dec->dict_limit) dec->dict_filled++;
}

static uint32_t lzma_dict_index(const lzma_decoder *dec, uint64_t position)
{
    if ((dec->dict_limit & (dec->dict_limit - 1u)) == 0)
        return (uint32_t)position & (dec->dict_limit - 1u);
    return (uint32_t)(position % dec->dict_limit);
}

lzma_decoder *lzma_dec_create(const lzma_props *props)
{
    size_t lit_count = (size_t)0x300 << (props->lc + props->lp);
    size_t total_lit = lit_count * sizeof(lzma_prob);
    lzma_decoder *dec = (lzma_decoder *)xx_mem_alloc(sizeof(lzma_decoder));
    if (!dec) return NULL;
    xx_rt_memset(dec, 0, sizeof(*dec));
    dec->props = *props;

    dec->lit_probs = (lzma_prob *)xx_mem_alloc(total_lit);
    if (!dec->lit_probs) { xx_mem_free(dec); return NULL; }

    dec->dict = (uint8_t *)xx_mem_alloc(props->dict_size);
    if (!dec->dict) { xx_mem_free(dec->lit_probs); xx_mem_free(dec); return NULL; }

    xx_rt_memset(dec->dict, 0, props->dict_size);

    dec->dict_mask = props->dict_size - 1;
    dec->dict_limit = props->dict_size;
    lzma_dec_reset_dictionary(dec);
    lzma_dec_reset_model(dec);
    dec->needs_init = false;
    return dec;
}

void lzma_dec_free(lzma_decoder *dec)
{
    if (!dec) return;
    xx_mem_free(dec->lit_probs);
    xx_mem_free(dec->dict);
    xx_mem_free(dec);
}

/* =========================================================================
 * Length decoder
 * ========================================================================= */

static uint32_t lzma_decode_len(lzma_range_dec *rd, lzma_decoder *dec,
                                int which, int pos_state)
{
    if (!rd_bit(rd, &dec->len_choice[which])) {
        return LZMA_MATCH_MIN_LEN + rd_bittree(rd, dec->len_low[which][pos_state], 3);
    }
    if (!rd_bit(rd, &dec->len_choice2[which])) {
        return LZMA_MATCH_MIN_LEN + 8 + rd_bittree(rd, dec->len_mid[which][pos_state], 3);
    }
    return LZMA_MATCH_MIN_LEN + 16 + rd_bittree(rd, dec->len_high[which], 8);
}

/* =========================================================================
 * Output helpers
 * ========================================================================= */

typedef struct {
    xx_io_device *dev;
    uint8_t      *mem;
    size_t        mem_cap;
    size_t        mem_written;
    uint64_t      total_written;
    uint8_t       obuf[65536];
    size_t        obuf_pos;
    bool          error;
    xx_pd_struct *pd;
} lzma_out;

static bool lzma_out_flush(lzma_out *o)
{
    if (!o->obuf_pos) return true;
    if (o->dev) {
        size_t done = 0;
        while (done < o->obuf_pos) {
            if (o->pd && xx_pd_is_stopped(o->pd)) { o->error = true; return false; }
            ssize_t amount = xx_io_write(o->dev, o->obuf + done, o->obuf_pos - done);
            if (amount <= 0 || (size_t)amount > o->obuf_pos - done) {
                o->error = true;
                return false;
            }
            done += (size_t)amount;
        }
    } else if (o->mem) {
        if (o->mem_written > o->mem_cap || o->obuf_pos > o->mem_cap - o->mem_written) {
            o->error = true;
            return false;
        }
        xx_rt_memcpy(o->mem + o->mem_written, o->obuf, o->obuf_pos);
        o->mem_written += o->obuf_pos;
    } else {
        o->error = true;
        return false;
    }
    o->total_written += o->obuf_pos;
    o->obuf_pos = 0;
    return true;
}

static bool lzma_out_byte(lzma_out *o, uint8_t b)
{
    if (o->obuf_pos >= sizeof(o->obuf)) if (!lzma_out_flush(o)) return false;
    o->obuf[o->obuf_pos++] = b;
    return true;
}

/* =========================================================================
 * LZMA block decompression
 * ========================================================================= */

static bool lzma_decode_block(lzma_range_dec *rd, lzma_decoder *dec,
                              int64_t uncomp_size, lzma_out *out,
                              xx_pd_struct *pd)
{
    int pb_mask = (1 << dec->props.pb) - 1;
    int lp_mask = (1 << dec->props.lp) - 1;

    int64_t total_out = 0;
    while (uncomp_size < 0 || total_out < uncomp_size) {
        if (rd->error || out->error || (pd && xx_pd_is_stopped(pd))) return false;

        uint64_t dp = dec->dict_pos;
        int pos_state = (int)dp & pb_mask;
        int state = dec->state;

        if (!rd_bit(rd, &dec->is_match[state][pos_state])) {
            /* Literal */
            uint8_t prev = dec->dict_filled ? dec->dict[lzma_dict_index(dec, dp - 1)] : 0;
            uint32_t lit_ctx = (((uint32_t)dp & (uint32_t)lp_mask) << dec->props.lc) |
                               ((uint32_t)prev >> (8 - dec->props.lc));
            lzma_prob *lit = dec->lit_probs + (size_t)lit_ctx * 0x300u;
            uint32_t sym;
            if (rd->error) return false;
            if (state < 7) {
                /* literal */
                sym = rd_bittree(rd, lit, 8);
            } else {
                /* literal with match byte */
                if (dec->rep[0] == 0 || dec->rep[0] > dec->dict_filled ||
                    dec->rep[0] > dec->dict_limit) return false;
                uint8_t match_byte = dec->dict[lzma_dict_index(dec, dp - dec->rep[0])];
                sym = 1;
                do {
                    int match_bit = (match_byte >> 7) & 1;
                    match_byte <<= 1;
                    int bit = rd_bit(rd, lit + ((1u + (uint32_t)match_bit) << 8) + sym);
                    sym = (sym << 1) | (uint32_t)bit;
                    if (match_bit != bit) {
                        while (sym < 0x100) {
                            sym = (sym << 1) | (uint32_t)rd_bit(rd, lit + sym);
                        }
                        break;
                    }
                } while (sym < 0x100);
                sym &= 0xFF;
            }
            if (rd->error) return false;
            dec->dict[lzma_dict_index(dec, dec->dict_pos++)] = (uint8_t)sym;
            lzma_dec_note_output(dec);
            if (!lzma_out_byte(out, (uint8_t)sym)) return false;
            dec->state = (state < 4) ? 0 : (state < 10) ? state - 3 : state - 6;
            total_out++;
            continue;
        }

        uint32_t len;
        if (!rd_bit(rd, &dec->is_rep[state])) {
            /* Match */
            uint32_t dist;
            len = lzma_decode_len(rd, dec, 0, pos_state);
            uint32_t len_state = (len - LZMA_MATCH_MIN_LEN < LZMA_NUM_LEN_TO_POS_STATES)
                                 ? len - LZMA_MATCH_MIN_LEN : LZMA_NUM_LEN_TO_POS_STATES - 1;
            uint32_t pos_slot = rd_bittree(rd, dec->pos_slot[len_state], 6);
            if (pos_slot < 4) {
                dist = pos_slot;
            } else {
                int direct_bits = (int)(pos_slot >> 1) - 1;
                dist = (2u | (pos_slot & 1u)) << direct_bits;
                if (pos_slot < 14) {
                    dist += rd_bittree_rev(rd, dec->pos_decoders + dist - pos_slot - 1, direct_bits);
                } else {
                    dist += rd_direct(rd, direct_bits - LZMA_NUM_ALIGN_BITS) << LZMA_NUM_ALIGN_BITS;
                    dist += rd_bittree_rev(rd, dec->pos_align, LZMA_NUM_ALIGN_BITS);
                    if (dist == 0xFFFFFFFFu) {
                        /* End-of-payload marker */
                        return uncomp_size < 0 && !rd->error;
                    }
                }
            }
            dec->rep[3] = dec->rep[2]; dec->rep[2] = dec->rep[1]; dec->rep[1] = dec->rep[0];
            dec->rep[0] = dist + 1;
            dec->state  = (state < 7) ? 7 : 10;
        } else {
            /* Rep match */
            if (!rd_bit(rd, &dec->is_rep_g0[state])) {
                if (!rd_bit(rd, &dec->is_rep0_long[state][pos_state])) {
                    /* Short rep */
                    if (dec->rep[0] == 0 || dec->rep[0] > dec->dict_filled ||
                        dec->rep[0] > dec->dict_limit || rd->error) return false;
                    dec->dict[lzma_dict_index(dec, dec->dict_pos)] =
                        dec->dict[lzma_dict_index(dec, dec->dict_pos - dec->rep[0])];
                    dec->dict_pos++;
                    lzma_dec_note_output(dec);
                    if (!lzma_out_byte(out, dec->dict[lzma_dict_index(dec, dec->dict_pos - 1)]))
                        return false;
                    dec->state = (state < 7) ? 9 : 11;
                    total_out++;
                    continue;
                }
                len = lzma_decode_len(rd, dec, 1, pos_state);
                dec->state = (state < 7) ? 8 : 11;
            } else if (!rd_bit(rd, &dec->is_rep_g1[state])) {
                uint32_t tmp = dec->rep[1]; dec->rep[1] = dec->rep[0]; dec->rep[0] = tmp;
                len = lzma_decode_len(rd, dec, 1, pos_state);
                dec->state = (state < 7) ? 8 : 11;
            } else if (!rd_bit(rd, &dec->is_rep_g2[state])) {
                uint32_t tmp = dec->rep[2]; dec->rep[2] = dec->rep[1]; dec->rep[1] = dec->rep[0]; dec->rep[0] = tmp;
                len = lzma_decode_len(rd, dec, 1, pos_state);
                dec->state = (state < 7) ? 8 : 11;
            } else {
                uint32_t tmp = dec->rep[3]; dec->rep[3] = dec->rep[2]; dec->rep[2] = dec->rep[1];
                dec->rep[1] = dec->rep[0]; dec->rep[0] = tmp;
                len = lzma_decode_len(rd, dec, 1, pos_state);
                dec->state = (state < 7) ? 8 : 11;
            }
        }

        /* Copy match */
        if (rd->error || dec->rep[0] == 0 || dec->rep[0] > dec->dict_filled ||
            dec->rep[0] > dec->dict_limit ||
            (uncomp_size >= 0 && (int64_t)len > uncomp_size - total_out)) return false;
        for (uint32_t j = 0; j < len; j++) {
            uint8_t b = dec->dict[lzma_dict_index(dec, dec->dict_pos - dec->rep[0])];
            dec->dict[lzma_dict_index(dec, dec->dict_pos++)] = b;
            lzma_dec_note_output(dec);
            if (!lzma_out_byte(out, b)) return false;
        }
        total_out += len;
    }
    return !rd->error && !out->error;
}

/* =========================================================================
 * Public LZMA decompression entry point
 * ========================================================================= */

bool xx_lzma_decompress_stream(lzma_range_dec *rd,
                               const lzma_props *props,
                               int64_t uncomp_size,
                               xx_io_device *dst_dev,
                               uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                               xx_pd_struct *pd)
{
    lzma_decoder *dec = lzma_dec_create(props);
    if (!dec) return false;

    lzma_out out;
    xx_rt_memset(&out, 0, sizeof(out));
    out.dev     = dst_dev;
    out.mem     = mem_dst;
    out.mem_cap = mem_cap;
    out.pd      = pd;

    bool ok = lzma_decode_block(rd, dec, uncomp_size, &out, pd);

    /* Flush remaining output */
    if (ok && out.obuf_pos > 0) ok = lzma_out_flush(&out);
    if (out_written) *out_written = ok ? (size_t)out.total_written : 0;

    lzma_dec_free(dec);
    return ok && !out.error;
}

/* =========================================================================
 * LZMA2 decompression
 * ========================================================================= */

bool xx_lzma2_decompress_stream(lzma_range_dec *rd,
                                uint8_t props2_byte,
                                xx_io_device *dst_dev,
                                uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd)
{
    uint64_t logical_dict;
    uint32_t allocation_size = 4096;
    uint8_t *packed = NULL;
    lzma_decoder *dec = NULL;
    bool dictionary_initialized = false;
    bool state_initialized = false;
    bool properties_initialized = false;
    bool saw_end = false;
    bool ok = false;
    lzma_out out;
    lzma_props props;

    if (out_written) *out_written = 0;
    if (!rd || (!dst_dev && !mem_dst && mem_cap != 0) ||
        props2_byte > 40 || rd_bytes_left(rd) == 0)
        return false;
    xx_rt_memset(&out, 0, sizeof(out));

    logical_dict = props2_byte == 40
        ? UINT32_MAX
        : ((uint64_t)2u | (uint64_t)(props2_byte & 1u)) << (props2_byte / 2 + 11);
    if (logical_dict > XX_LZMA_MAX_DICT_SIZE) return false;
    while ((uint64_t)allocation_size < logical_dict) allocation_size <<= 1;

    xx_rt_memset(&props, 0, sizeof(props));
    props.lc = 3;
    props.lp = 0;
    props.pb = 2;
    props.dict_size = allocation_size;
    dec = lzma_dec_create(&props);
    if (!dec) return false;
    dec->dict_limit = (uint32_t)logical_dict;

    packed = (uint8_t *)xx_mem_alloc(65536u);
    if (!packed) goto cleanup;

    out.dev = dst_dev;
    out.mem = mem_dst;
    out.mem_cap = mem_cap;
    out.pd = pd;

    while (!saw_end) {
        uint8_t ctrl;
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        ctrl = rd_byte(rd);
        if (rd->error) goto cleanup;

        if (ctrl == LZMA2_CONTROL_EOF) {
            if (rd_bytes_left(rd) != 0) goto cleanup;
            saw_end = true;
            break;
        }

        if (ctrl == LZMA2_CONTROL_COPY_NO_DICT || ctrl == LZMA2_CONTROL_COPY_DICT) {
            uint8_t size_bytes[2];
            uint32_t chunk_size;
            if (!rd_read_exact(rd, size_bytes, sizeof(size_bytes), pd)) goto cleanup;
            chunk_size = (((uint32_t)size_bytes[0] << 8) | size_bytes[1]) + 1u;
            if (ctrl == LZMA2_CONTROL_COPY_NO_DICT) {
                lzma_dec_reset_dictionary(dec);
                dictionary_initialized = true;
            } else if (!dictionary_initialized) {
                goto cleanup;
            }
            state_initialized = false;
            for (uint32_t i = 0; i < chunk_size; i++) {
                uint8_t value;
                if (pd && (i & 0xFFFu) == 0 && xx_pd_is_stopped(pd)) goto cleanup;
                value = rd_byte(rd);
                if (rd->error) goto cleanup;
                dec->dict[lzma_dict_index(dec, dec->dict_pos++)] = value;
                lzma_dec_note_output(dec);
                if (!lzma_out_byte(&out, value)) goto cleanup;
            }
            continue;
        }

        if (ctrl < 0x80) goto cleanup;
        {
            uint8_t header[4];
            uint8_t mode = (uint8_t)((ctrl >> 5) & 3u);
            uint32_t unpack_size;
            uint32_t packed_size;
            lzma_range_dec chunk_rd;

            if (!rd_read_exact(rd, header, sizeof(header), pd)) goto cleanup;
            unpack_size = (((uint32_t)ctrl & 0x1Fu) << 16) |
                          ((uint32_t)header[0] << 8) | header[1];
            packed_size = ((uint32_t)header[2] << 8) | header[3];
            unpack_size++;
            packed_size++;

            if (mode == 3) {
                lzma_dec_reset_dictionary(dec);
                dictionary_initialized = true;
            } else if (!dictionary_initialized) {
                goto cleanup;
            }

            if (mode >= 2) {
                uint8_t property = rd_byte(rd);
                int lc, lp, pb;
                if (rd->error || property >= 9 * 5 * 5) goto cleanup;
                lc = property % 9;
                property = (uint8_t)(property / 9);
                lp = property % 5;
                pb = property / 5;
                if (!lzma_dec_set_lclppb(dec, lc, lp, pb)) goto cleanup;
                properties_initialized = true;
            } else if (!properties_initialized) {
                goto cleanup;
            }

            if (mode >= 1) {
                lzma_dec_reset_model(dec);
                state_initialized = true;
            } else if (!state_initialized) {
                goto cleanup;
            }

            if (packed_size < RC_INIT_BYTES ||
                !rd_read_exact(rd, packed, packed_size, pd) ||
                !lzma_rd_init(&chunk_rd, NULL, packed, packed_size, (int64_t)packed_size) ||
                !lzma_decode_block(&chunk_rd, dec, (int64_t)unpack_size, &out, pd) ||
                chunk_rd.error) goto cleanup;
            state_initialized = true;
        }
    }

    if (!saw_end) goto cleanup;
    if (out.obuf_pos && !lzma_out_flush(&out)) goto cleanup;
    ok = true;

cleanup:
    if (out_written) *out_written = ok ? (size_t)out.total_written : 0;
    xx_mem_free(packed);
    lzma_dec_free(dec);
    return ok;
}
