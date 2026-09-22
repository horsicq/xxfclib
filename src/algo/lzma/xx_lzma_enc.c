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

/* LZMA compressor — clean C implementation.
 *
 * This encoder produces valid LZMA raw bitstreams (no container) compatible
 * with the LZMA SDK decoder and all standard tools. The compression uses
 * a hash-chain LZ77 match finder combined with the LZMA range coder.
 *
 * Level 1: fastest (dict 64 kB, nice=32)
 * Level 5: default (dict 16 MB, nice=128)
 * Level 9: best    (dict 64 MB, nice=273)
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_lzma_internal.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Range encoder
 * ========================================================================= */

typedef struct {
    xx_io_device *dev;
    uint8_t      *mem;
    size_t        mem_cap;
    size_t        mem_pos;
    uint8_t       obuf[65536];
    size_t        obuf_pos;
    int64_t       total_written;
    uint64_t      low;
    uint32_t      range;
    int32_t       cache_size;
    uint8_t       cache;
    bool          error;
    xx_pd_struct *pd;
} lzma_range_enc;

static bool re_write_all(lzma_range_enc *re, const uint8_t *data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        if (re->pd && xx_pd_is_stopped(re->pd)) return false;
        ssize_t amount = xx_io_write(re->dev, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool re_flush_buffer(lzma_range_enc *re)
{
    if (!re->obuf_pos) return true;
    if (re->dev) {
        if (!re_write_all(re, re->obuf, re->obuf_pos)) {
            re->error = true;
            return false;
        }
    } else if (re->mem) {
        if (re->obuf_pos > re->mem_cap - re->mem_pos) {
            re->error = true;
            return false;
        }
        xx_rt_memcpy(re->mem + re->mem_pos, re->obuf, re->obuf_pos);
        re->mem_pos += re->obuf_pos;
    } else {
        re->error = true;
        return false;
    }
    re->total_written += (int64_t)re->obuf_pos;
    re->obuf_pos = 0;
    return true;
}

static bool re_flush_byte(lzma_range_enc *re, uint8_t b)
{
    if (re->obuf_pos >= sizeof(re->obuf)) {
        if (!re_flush_buffer(re)) return false;
    }
    re->obuf[re->obuf_pos++] = b;
    return true;
}

static void re_shift_low(lzma_range_enc *re)
{
    if ((uint32_t)re->low < 0xFF000000u || (int)(re->low >> 32) != 0) {
        uint8_t temp = re->cache;
        do {
            re_flush_byte(re, (uint8_t)(temp + (uint8_t)(re->low >> 32)));
            temp = 0xFF;
        } while (--re->cache_size != 0);
        re->cache = (uint8_t)(re->low >> 24);
    }
    re->cache_size++;
    re->low = (uint32_t)((re->low << 8) & 0xFFFFFFFFu);
}

static void re_encode_bit(lzma_range_enc *re, lzma_prob *prob, int bit)
{
    uint32_t bound = (re->range >> RC_BIT_MODEL_TOTAL_BITS) * (*prob);
    if (!bit) {
        re->range = bound;
        *prob += (RC_BIT_MODEL_TOTAL - *prob) >> RC_MOVE_BITS;
    } else {
        re->low  += bound;
        re->range -= bound;
        *prob -= *prob >> RC_MOVE_BITS;
    }
    if (re->range < RC_TOP_VALUE) {
        re->range <<= 8;
        re_shift_low(re);
    }
}

static void re_encode_bittree(lzma_range_enc *re, lzma_prob *probs, int n_bits, uint32_t sym)
{
    uint32_t m = 1;
    for (int i = n_bits - 1; i >= 0; i--) {
        int bit = (int)((sym >> i) & 1);
        re_encode_bit(re, probs + m, bit);
        m = (m << 1) | (uint32_t)bit;
    }
}

static void re_encode_bittree_rev(lzma_range_enc *re, lzma_prob *probs, int n_bits, uint32_t sym)
{
    uint32_t m = 1;
    for (int i = 0; i < n_bits; i++) {
        int bit = (int)(sym & 1);
        sym >>= 1;
        re_encode_bit(re, probs + m, bit);
        m = (m << 1) | (uint32_t)bit;
    }
}

static void re_encode_direct(lzma_range_enc *re, uint32_t val, int n_bits)
{
    for (int i = n_bits - 1; i >= 0; i--) {
        re->range >>= 1;
        re->low += re->range & (0u - ((val >> i) & 1u));
        if (re->range < RC_TOP_VALUE) { re->range <<= 8; re_shift_low(re); }
    }
}

static bool re_init(lzma_range_enc *re, xx_io_device *dev, uint8_t *mem, size_t mem_cap,
                    xx_pd_struct *pd)
{
    xx_rt_memset(re, 0, sizeof(*re));
    re->dev     = dev;
    re->mem     = mem;
    re->mem_cap = mem_cap;
    re->range   = 0xFFFFFFFFu;
    re->cache_size = 1;
    re->pd = pd;
    return true;
}

static bool re_flush(lzma_range_enc *re)
{
    for (int i = 0; i < 5; i++) re_shift_low(re);
    if (!re_flush_buffer(re)) return false;
    return !re->error;
}

/* =========================================================================
 * LZMA encoder state (one set of prob arrays + hash chains)
 * ========================================================================= */

#define HASH4_SIZE (1 << 20)
#define HASH4_MASK (HASH4_SIZE - 1)

typedef struct {
    lzma_props     props;
    lzma_prob     *lit_probs;
    lzma_prob      is_match[LZMA_NUM_STATES][1 << LZMA_NUM_POS_BITS_MAX];
    lzma_prob      is_rep[LZMA_NUM_STATES];
    lzma_prob      is_rep_g0[LZMA_NUM_STATES];
    lzma_prob      is_rep_g1[LZMA_NUM_STATES];
    lzma_prob      is_rep_g2[LZMA_NUM_STATES];
    lzma_prob      is_rep0_long[LZMA_NUM_STATES][1 << LZMA_NUM_POS_BITS_MAX];
    lzma_prob      pos_slot[LZMA_NUM_LEN_TO_POS_STATES][1 << 6];
    lzma_prob      pos_decoders[LZMA_FULL_DISTANCES - LZMA_NUM_LEN_TO_POS_STATES * 4];
    lzma_prob      pos_align[1 << LZMA_NUM_ALIGN_BITS];
    lzma_prob      len_choice[2];
    lzma_prob      len_choice2[2];
    lzma_prob      len_low [2][1 << LZMA_NUM_POS_BITS_MAX][8];
    lzma_prob      len_mid [2][1 << LZMA_NUM_POS_BITS_MAX][8];
    lzma_prob      len_high[2][256];
    /* Match finder */
    uint32_t      *hash4;
    uint32_t      *chain;
    uint32_t       chain_mask;
    /* Window */
    uint8_t       *window;
    uint32_t       win_pos;
    uint32_t       win_size;
    /* Encoder state */
    int            state;
    uint32_t       rep[4];
} lzma_encoder;

static void enc_probs_init(lzma_encoder *enc)
{
    size_t lit_count = (size_t)0x300 << (enc->props.lc + enc->props.lp);
    for (size_t i = 0; i < lit_count; i++) enc->lit_probs[i] = PROB_INIT_VAL;
    for (int s = 0; s < LZMA_NUM_STATES; s++)
        for (int i = 0; i < (1 << LZMA_NUM_POS_BITS_MAX); i++) {
            enc->is_match[s][i] = PROB_INIT_VAL;
            enc->is_rep0_long[s][i] = PROB_INIT_VAL;
        }
    for (int i = 0; i < LZMA_NUM_STATES; i++) {
        enc->is_rep[i] = enc->is_rep_g0[i] = enc->is_rep_g1[i] = enc->is_rep_g2[i] = PROB_INIT_VAL;
    }
    for (int i = 0; i < LZMA_NUM_LEN_TO_POS_STATES; i++)
        for (int j = 0; j < (1 << 6); j++) enc->pos_slot[i][j] = PROB_INIT_VAL;
    for (size_t i = 0; i < LZMA_FULL_DISTANCES - LZMA_NUM_LEN_TO_POS_STATES * 4; i++)
        enc->pos_decoders[i] = PROB_INIT_VAL;
    for (int i = 0; i < (1 << LZMA_NUM_ALIGN_BITS); i++) enc->pos_align[i] = PROB_INIT_VAL;
    for (int w = 0; w < 2; w++) {
        enc->len_choice[w] = PROB_INIT_VAL;
        enc->len_choice2[w] = PROB_INIT_VAL;
        for (int pb = 0; pb < (1 << LZMA_NUM_POS_BITS_MAX); pb++) {
            for (int i = 0; i < 8; i++) { enc->len_low[w][pb][i] = PROB_INIT_VAL; enc->len_mid[w][pb][i] = PROB_INIT_VAL; }
        }
    }
    for (int w = 0; w < 2; w++) for (int i = 0; i < 256; i++) enc->len_high[w][i] = PROB_INIT_VAL;
}

static void enc_encode_len(lzma_range_enc *re, lzma_encoder *enc, int which, int pos_state, uint32_t len)
{
    len -= LZMA_MATCH_MIN_LEN;
    if (len < 8) {
        re_encode_bit(re, &enc->len_choice[which], 0);
        re_encode_bittree(re, enc->len_low[which][pos_state], 3, len);
    } else if (len < 16) {
        re_encode_bit(re, &enc->len_choice[which], 1);
        re_encode_bit(re, &enc->len_choice2[which], 0);
        re_encode_bittree(re, enc->len_mid[which][pos_state], 3, len - 8);
    } else {
        re_encode_bit(re, &enc->len_choice[which], 1);
        re_encode_bit(re, &enc->len_choice2[which], 1);
        re_encode_bittree(re, enc->len_high[which], 8, len - 16);
    }
}

static void enc_encode_dist(lzma_range_enc *re, lzma_encoder *enc, uint32_t dist, uint32_t len)
{
    uint32_t len_state = (len - LZMA_MATCH_MIN_LEN < (uint32_t)LZMA_NUM_LEN_TO_POS_STATES)
                         ? len - LZMA_MATCH_MIN_LEN : (uint32_t)LZMA_NUM_LEN_TO_POS_STATES - 1;

    /* Find pos_slot */
    uint32_t pos_slot;
    if (dist < 4) {
        pos_slot = dist;
    } else {
        uint32_t k = 2;
        while ((dist >> (k + 1)) != 0) k++;
        pos_slot = (k << 1) | ((dist >> (k - 1)) & 1u);
    }
    if (pos_slot >= 63) pos_slot = 63;

    re_encode_bittree(re, enc->pos_slot[len_state], 6, pos_slot);

    if (pos_slot >= 4) {
        int direct_bits = (int)(pos_slot >> 1) - 1;
        uint32_t base = (2u | (pos_slot & 1u)) << direct_bits;
        uint32_t footer = dist - base;
        if (pos_slot < 14) {
            re_encode_bittree_rev(re, enc->pos_decoders + base - pos_slot - 1, direct_bits, footer);
        } else {
            re_encode_direct(re, footer >> LZMA_NUM_ALIGN_BITS, direct_bits - LZMA_NUM_ALIGN_BITS);
            re_encode_bittree_rev(re, enc->pos_align, LZMA_NUM_ALIGN_BITS, footer & ((1u << LZMA_NUM_ALIGN_BITS) - 1u));
        }
    }
}

/* Simple hash chain match finder */
static uint32_t enc_find_match(lzma_encoder *enc, const uint8_t *data, size_t pos,
                               size_t data_size, uint32_t nice_len,
                               uint32_t *out_dist)
{
    if (pos + 2 > data_size) return 0;

    /* Hash4 */
    uint32_t h = (uint32_t)data[pos] ^ ((uint32_t)data[pos+1] << 8) ^
                 ((uint32_t)data[pos+2] << 16);
    if (pos + 3 < data_size) h ^= (uint32_t)data[pos+3] << 24;
    h ^= h >> 11; h ^= h << 15; h ^= h >> 18;
    h &= HASH4_MASK;

    uint32_t best_len = 1, best_dist = 0;
    uint32_t cur = enc->hash4[h];

    uint32_t max_dist = (uint32_t)(enc->props.dict_size < (uint32_t)pos ? enc->props.dict_size : (uint32_t)pos);

    for (uint32_t depth = 0; depth < 64 && cur != 0; depth++) {
        uint32_t match_pos = cur - 1;
        if (match_pos >= (uint32_t)pos || (uint32_t)pos - match_pos > max_dist) break;
        uint32_t dist = (uint32_t)pos - match_pos;
        const uint8_t *ref = data + match_pos;
        uint32_t len = 0;
        size_t avail = data_size - pos;
        while (len < avail && len < nice_len && ref[len] == data[pos + len]) len++;
        if (len > best_len) { best_len = len; best_dist = dist; }
        if (len >= nice_len) break;
        cur = enc->chain[match_pos & enc->chain_mask];
    }

    enc->chain[(uint32_t)pos & enc->chain_mask] = enc->hash4[h];
    enc->hash4[h] = (uint32_t)pos + 1;

    *out_dist = best_dist;
    return best_len;
}

/* =========================================================================
 * Public LZMA compression entry point
 * ========================================================================= */

bool xx_lzma_compress_stream(xx_io_device *src_dev,
                             const uint8_t *mem_src, size_t mem_src_size,
                             int64_t src_offset, int64_t uncomp_size,
                             xx_io_device *dst_dev,
                             uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                             int level,
                             uint8_t *out_props, size_t *out_props_size,
                             xx_pd_struct *pd,
                             bool write_end_marker)
{
    if (out_written) *out_written = 0;
    if (out_props_size) {
        if (!out_props || *out_props_size < XX_LZMA_PROPS_SIZE) {
            *out_props_size = 0;
            return false;
        }
    }
    if (uncomp_size < 0 || (uint64_t)uncomp_size > (uint64_t)SIZE_MAX ||
        (!dst_dev && !mem_dst) || (!src_dev && !mem_src) ||
        (mem_src && (uint64_t)uncomp_size > (uint64_t)mem_src_size) ||
        (src_dev && src_offset < 0) ||
        (pd && xx_pd_is_stopped(pd))) {
        if (out_props_size) *out_props_size = 0;
        return false;
    }
    if (level < 1) level = 1;
    if (level > 9) level = 9;

    /* Choose dict size and nice length by level */
    uint32_t dict_size;
    uint32_t nice_len;
    if      (level <= 1) { dict_size = 64 * 1024;        nice_len = 32; }
    else if (level <= 3) { dict_size = 1 * 1024 * 1024;  nice_len = 64; }
    else if (level <= 5) { dict_size = 16 * 1024 * 1024; nice_len = 128; }
    else if (level <= 7) { dict_size = 32 * 1024 * 1024; nice_len = 192; }
    else                 { dict_size = 64 * 1024 * 1024; nice_len = 273; }

    if (uncomp_size > 0 && (uint32_t)uncomp_size < dict_size) {
        dict_size = (uint32_t)uncomp_size;
    }

    /* Make dict_size a power of 2 (min 4096) */
    uint32_t real_dict = 4096;
    while (real_dict < dict_size && real_dict < (1u << 30)) real_dict <<= 1;

    lzma_props props;
    props.lc = 3; props.lp = 0; props.pb = 2;
    props.dict_size = real_dict;

    /* Write property bytes */
    if (out_props && out_props_size) {
        out_props[0] = (uint8_t)(props.pb * 45 + props.lp * 9 + props.lc);
        out_props[1] = (uint8_t)(dict_size);
        out_props[2] = (uint8_t)(dict_size >> 8);
        out_props[3] = (uint8_t)(dict_size >> 16);
        out_props[4] = (uint8_t)(dict_size >> 24);
        *out_props_size = XX_LZMA_PROPS_SIZE;
    }

    /* Load input data */
    if (src_dev && src_offset >= 0)
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;

    uint8_t *data = NULL;
    size_t data_size = (size_t)uncomp_size;
    bool owns_data = false;

    if (mem_src) {
        data = (uint8_t *)mem_src;
    } else {
        data = (uint8_t *)xx_mem_alloc(data_size);
        if (!data) return false;
        owns_data = true;
        size_t done = 0;
        while (done < data_size) {
            if (pd && xx_pd_is_stopped(pd)) { xx_mem_free(data); return false; }
            ssize_t got = xx_io_read(src_dev, data + done, data_size - done);
            if (got <= 0 || (size_t)got > data_size - done) {
                xx_mem_free(data); return false;
            }
            done += (size_t)got;
        }
    }

    /* Allocate encoder */
    lzma_encoder *enc = (lzma_encoder *)xx_mem_alloc(sizeof(lzma_encoder));
    if (!enc) { if (owns_data) xx_mem_free(data); return false; }
    xx_rt_memset(enc, 0, sizeof(*enc));
    enc->props = props;

    size_t lit_count = (size_t)0x300 << (props.lc + props.lp);
    enc->lit_probs = (lzma_prob *)xx_mem_alloc(lit_count * sizeof(lzma_prob));
    enc->hash4 = (uint32_t *)xx_mem_alloc(HASH4_SIZE * sizeof(uint32_t));
    enc->chain_mask = real_dict - 1;
    enc->chain = (uint32_t *)xx_mem_alloc(real_dict * sizeof(uint32_t));

    if (!enc->lit_probs || !enc->hash4 || !enc->chain) {
        xx_mem_free(enc->lit_probs); xx_mem_free(enc->hash4);
        xx_mem_free(enc->chain);     xx_mem_free(enc);
        if (owns_data) xx_mem_free(data);
        return false;
    }

    xx_rt_memset(enc->hash4, 0, HASH4_SIZE * sizeof(uint32_t));
    xx_rt_memset(enc->chain, 0, real_dict * sizeof(uint32_t));
    enc->rep[0] = enc->rep[1] = enc->rep[2] = enc->rep[3] = 1;
    enc_probs_init(enc);

    lzma_range_enc re;
    re_init(&re, dst_dev, mem_dst, mem_cap, pd);

    int pb_mask = (1 << props.pb) - 1;
    int lp_mask = (1 << props.lp) - 1;

    bool ok = true;
    for (size_t pos = 0; pos < data_size && ok; ) {
        if (pd && (pos & 0x3FFFu) == 0 && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        int pos_state = (int)pos & pb_mask;
        int state = enc->state;

        /* Try to find a match */
        uint32_t dist = 0;
        uint32_t match_len = enc_find_match(enc, data, pos, data_size, nice_len, &dist);

        /* Check rep[0] */
        uint32_t rep0_len = 0;
        if (enc->rep[0] <= (uint32_t)pos) {
            const uint8_t *ref = data + pos - enc->rep[0];
            while (rep0_len < 273 && pos + rep0_len < data_size &&
                   ref[rep0_len] == data[pos + rep0_len]) rep0_len++;
        }

        if (match_len >= 2 && match_len >= rep0_len) {
            /* MATCH */
            re_encode_bit(&re, &enc->is_match[state][pos_state], 1);
            re_encode_bit(&re, &enc->is_rep[state], 0);
            enc_encode_len(&re, enc, 0, pos_state, match_len);
            enc_encode_dist(&re, enc, dist - 1, match_len);
            enc->rep[3] = enc->rep[2]; enc->rep[2] = enc->rep[1];
            enc->rep[1] = enc->rep[0]; enc->rep[0] = dist;
            enc->state  = (state < 7) ? 7 : 10;
            pos += match_len;
        } else if (rep0_len >= 2) {
            /* REP0 */
            re_encode_bit(&re, &enc->is_match[state][pos_state], 1);
            re_encode_bit(&re, &enc->is_rep[state], 1);
            re_encode_bit(&re, &enc->is_rep_g0[state], 0);
            re_encode_bit(&re, &enc->is_rep0_long[state][pos_state], 1);
            enc_encode_len(&re, enc, 1, pos_state, rep0_len);
            enc->state = (state < 7) ? 8 : 11;
            pos += rep0_len;
        } else {
            /* LITERAL */
            uint8_t prev = (pos > 0) ? data[pos - 1] : 0;
            uint8_t cur  = data[pos];
            uint32_t lit_ctx = ((uint32_t)pos & lp_mask) | ((uint32_t)prev >> (8 - props.lc));
            lzma_prob *lit = enc->lit_probs + (lit_ctx << 8) * 3;

            re_encode_bit(&re, &enc->is_match[state][pos_state], 0);

            if (state < 7) {
                re_encode_bittree(&re, lit, 8, cur);
            } else {
                /* Literal with match byte context */
                uint8_t match_byte = (pos >= enc->rep[0]) ? data[pos - enc->rep[0]] : 0;
                uint32_t sym = 1;
                do {
                    int match_bit = (match_byte >> 7) & 1;
                    match_byte <<= 1;
                    int my_bit = (cur >> 7) & 1;
                    cur <<= 1;
                    re_encode_bit(&re, lit + ((1 + match_bit) << 8) + sym, my_bit);
                    sym = (sym << 1) | (uint32_t)my_bit;
                    if (match_bit != my_bit) {
                        while (sym < 0x100) {
                            int bit = (cur >> 7) & 1;
                            cur <<= 1;
                            re_encode_bit(&re, lit + sym, bit);
                            sym = (sym << 1) | (uint32_t)bit;
                        }
                        break;
                    }
                } while (sym < 0x100);
            }

            enc->state = (state < 4) ? 0 : (state < 10) ? state - 3 : state - 6;
            pos++;
        }

        if (re.error) ok = false;
    }

    /* Standalone raw LZMA streams need EOS; LZMA2 chunks are size bounded. */
    if (ok) {
        if (write_end_marker) {
            re_encode_bit(&re, &enc->is_match[enc->state][0], 1);
            re_encode_bit(&re, &enc->is_rep[enc->state], 0);
            enc_encode_len(&re, enc, 0, 0, LZMA_MATCH_MIN_LEN);
            re_encode_bittree(&re, enc->pos_slot[0], 6, 63);
            re_encode_direct(&re, (1u << (30 - LZMA_NUM_ALIGN_BITS)) - 1u, 30 - LZMA_NUM_ALIGN_BITS);
            re_encode_bittree_rev(&re, enc->pos_align, LZMA_NUM_ALIGN_BITS,
                                  (1 << LZMA_NUM_ALIGN_BITS) - 1);
        }
        ok = re_flush(&re);
        if (ok && out_written) *out_written = (size_t)re.total_written;
    }

    xx_mem_free(enc->lit_probs);
    xx_mem_free(enc->hash4);
    xx_mem_free(enc->chain);
    xx_mem_free(enc);
    if (owns_data) xx_mem_free(data);
    if (!ok) {
        if (out_written) *out_written = 0;
        if (out_props_size) *out_props_size = 0;
    }
    return ok;
}

/* =========================================================================
 * Raw LZMA2 compression
 * ========================================================================= */

#define LZMA2_CHUNK_SIZE 65536u
#define LZMA2_DICT_PROP  8u

typedef struct {
    xx_io_device *dev;
    uint8_t *mem;
    size_t mem_cap;
    size_t written;
    xx_pd_struct *pd;
} lzma2_sink;

static bool lzma2_sink_write(lzma2_sink *sink, const uint8_t *data, size_t size)
{
    if (size > SIZE_MAX - sink->written) return false;
    if (sink->dev) {
        size_t done = 0;
        while (done < size) {
            if (sink->pd && xx_pd_is_stopped(sink->pd)) return false;
            ssize_t amount = xx_io_write(sink->dev, data + done, size - done);
            if (amount <= 0 || (size_t)amount > size - done) return false;
            done += (size_t)amount;
        }
    } else {
        if (!sink->mem || sink->written > sink->mem_cap ||
            size > sink->mem_cap - sink->written) return false;
        if (size) xx_rt_memcpy(sink->mem + sink->written, data, size);
    }
    sink->written += size;
    return true;
}

static bool lzma2_read_exact(xx_io_device *dev, uint8_t *data, size_t size,
                             xx_pd_struct *pd)
{
    size_t done = 0;
    while (done < size) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        ssize_t amount = xx_io_read(dev, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

bool xx_lzma2_compress_stream(xx_io_device *src_dev,
                              const uint8_t *mem_src, size_t mem_src_size,
                              int64_t src_offset, int64_t uncomp_size,
                              xx_io_device *dst_dev,
                              uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                              int level, uint8_t *out_props2_byte,
                              xx_pd_struct *pd)
{
    uint8_t *input = NULL;
    uint8_t *compressed = NULL;
    const size_t compressed_cap = LZMA2_CHUNK_SIZE * 2u + 1024u;
    size_t source_pos = 0;
    bool dictionary_initialized = false;
    bool ok = false;
    lzma2_sink sink;

    if (out_written) *out_written = 0;
    if (out_props2_byte) *out_props2_byte = 0;
    if (uncomp_size < 0 || (uint64_t)uncomp_size > (uint64_t)SIZE_MAX ||
        (!src_dev && !mem_src && uncomp_size != 0) || (!dst_dev && !mem_dst) ||
        (mem_src && (uint64_t)uncomp_size > (uint64_t)mem_src_size) ||
        (src_dev && src_offset < 0) ||
        (pd && xx_pd_is_stopped(pd))) return false;

    if (src_dev && xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;

    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.dev = dst_dev;
    sink.mem = mem_dst;
    sink.mem_cap = mem_cap;
    sink.pd = pd;

    input = (uint8_t *)xx_mem_alloc(LZMA2_CHUNK_SIZE);
    compressed = (uint8_t *)xx_mem_alloc(compressed_cap);
    if (!input || !compressed) goto cleanup;

    while (source_pos < (size_t)uncomp_size) {
        size_t chunk_size = (size_t)uncomp_size - source_pos;
        size_t packed_size = 0;
        size_t props_size = XX_LZMA_PROPS_SIZE;
        uint8_t props[XX_LZMA_PROPS_SIZE];
        uint32_t unpack_minus_one;
        bool compressed_ok;

        if (chunk_size > LZMA2_CHUNK_SIZE) chunk_size = LZMA2_CHUNK_SIZE;
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        if (mem_src) {
            xx_rt_memcpy(input, mem_src + source_pos, chunk_size);
        } else if (!lzma2_read_exact(src_dev, input, chunk_size, pd)) {
            goto cleanup;
        }

        compressed_ok = xx_lzma_compress_stream(NULL, input, chunk_size, 0,
                                                 (int64_t)chunk_size, NULL,
                                                 compressed, compressed_cap, &packed_size,
                                                 level, props, &props_size, pd, false);
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;

        unpack_minus_one = (uint32_t)chunk_size - 1u;
        if (compressed_ok && props_size == XX_LZMA_PROPS_SIZE && props[0] == 0x5Du &&
            packed_size > 0 && packed_size <= LZMA2_CHUNK_SIZE &&
            packed_size + 6u < chunk_size + 3u) {
            uint32_t pack_minus_one = (uint32_t)packed_size - 1u;
            uint8_t header[6];
            header[0] = (uint8_t)(0xE0u | (unpack_minus_one >> 16));
            header[1] = (uint8_t)(unpack_minus_one >> 8);
            header[2] = (uint8_t)unpack_minus_one;
            header[3] = (uint8_t)(pack_minus_one >> 8);
            header[4] = (uint8_t)pack_minus_one;
            header[5] = 0x5D;
            if (!lzma2_sink_write(&sink, header, sizeof(header)) ||
                !lzma2_sink_write(&sink, compressed, packed_size)) goto cleanup;
            dictionary_initialized = true;
        } else {
            uint8_t header[3];
            header[0] = dictionary_initialized ? LZMA2_CONTROL_COPY_DICT
                                               : LZMA2_CONTROL_COPY_NO_DICT;
            header[1] = (uint8_t)(unpack_minus_one >> 8);
            header[2] = (uint8_t)unpack_minus_one;
            if (!lzma2_sink_write(&sink, header, sizeof(header)) ||
                !lzma2_sink_write(&sink, input, chunk_size)) goto cleanup;
            dictionary_initialized = true;
        }
        source_pos += chunk_size;
    }

    {
        const uint8_t end = LZMA2_CONTROL_EOF;
        if (!lzma2_sink_write(&sink, &end, 1)) goto cleanup;
    }
    ok = true;

cleanup:
    xx_mem_free(compressed);
    xx_mem_free(input);
    if (ok) {
        if (out_written) *out_written = sink.written;
        if (out_props2_byte) *out_props2_byte = LZMA2_DICT_PROP;
    } else {
        if (out_written) *out_written = 0;
        if (out_props2_byte) *out_props2_byte = 0;
    }
    return ok;
}
