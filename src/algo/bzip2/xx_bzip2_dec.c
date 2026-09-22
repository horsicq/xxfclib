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

/* Bzip2 decompressor — clean C implementation.
 *
 * Algorithm: Burrows-Wheeler Transform (BWT) + Move-to-Front (MTF) +
 * run-length encoding + canonical Huffman coding, as defined by the bzip2
 * format specification (Julian Seward, 1996-2019).
 *
 * This implementation is an independent rewrite using the same algorithm
 * with xx_io_device I/O and xxfclib memory primitives.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_bzip2_internal.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include <string.h>

/* =========================================================================
 * Bit-reader helpers
 * ========================================================================= */

static bool bz2_br_refill(bz2_bit_reader *br)
{
    if (br->error || br->eof) return false;
    if (br->ibuf_pos < br->ibuf_len) return true;

    size_t want = sizeof(br->ibuf);
    if (br->remaining >= 0) {
        if (br->remaining == 0) { br->eof = true; return false; }
        if ((int64_t)want > br->remaining) want = (size_t)br->remaining;
    }

    ssize_t got;
    if (br->dev) {
        got = xx_io_read(br->dev, br->ibuf, want);
    } else {
        size_t avail = br->mem_size - br->mem_pos;
        got = (avail == 0) ? 0 : (ssize_t)(avail < want ? avail : want);
        if (got > 0) {
            xx_rt_memcpy(br->ibuf, br->mem + br->mem_pos, (size_t)got);
            br->mem_pos += (size_t)got;
        }
    }

    if (got <= 0) { br->eof = true; return false; }
    if (br->remaining >= 0) br->remaining -= got;
    br->ibuf_pos = 0;
    br->ibuf_len = (size_t)got;
    return true;
}

bool bz2_br_init(bz2_bit_reader *br, xx_io_device *dev,
                 const uint8_t *mem, size_t mem_size, int64_t remaining)
{
    xx_rt_memset(br, 0, sizeof(*br));
    br->dev       = dev;
    br->mem       = mem;
    br->mem_size  = mem_size;
    br->remaining = remaining;
    return true;
}

void bz2_br_free(bz2_bit_reader *br)
{
    (void)br;
}

static uint32_t bz2_read_bits(bz2_bit_reader *br, int n)
{
    while (br->n_bits < n) {
        if (!bz2_br_refill(br)) { br->error = true; return 0; }
        br->bits = (br->bits << 8) | (uint64_t)br->ibuf[br->ibuf_pos++];
        br->n_bits += 8;
    }
    br->n_bits -= n;
    uint32_t mask = (n == 32) ? 0xFFFFFFFFu : (uint32_t)((1u << n) - 1u);
    return (uint32_t)(br->bits >> br->n_bits) & mask;
}

static uint32_t bz2_read_bit(bz2_bit_reader *br)
{
    return bz2_read_bits(br, 1);
}

/* =========================================================================
 * Bit-writer helpers
 * ========================================================================= */

bool bz2_bw_init(bz2_bit_writer *bw, xx_io_device *dev, uint8_t *mem, size_t mem_cap)
{
    xx_rt_memset(bw, 0, sizeof(*bw));
    bw->dev     = dev;
    bw->mem     = mem;
    bw->mem_cap = mem_cap;
    return true;
}

static bool bz2_bw_flush_buffer(bz2_bit_writer *bw)
{
    if (bw->obuf_pos > 0) {
        if (bw->dev) {
            ssize_t w = xx_io_write(bw->dev, bw->obuf, bw->obuf_pos);
            if (w < 0 || (size_t)w != bw->obuf_pos) { bw->error = true; return false; }
        } else if (bw->mem) {
            if (bw->mem_pos + bw->obuf_pos > bw->mem_cap) { bw->error = true; return false; }
            xx_rt_memcpy(bw->mem + bw->mem_pos, bw->obuf, bw->obuf_pos);
            bw->mem_pos += bw->obuf_pos;
        }
        bw->total_written += (int64_t)bw->obuf_pos;
        bw->obuf_pos = 0;
    }
    return !bw->error;
}

static bool bz2_bw_write_byte(bz2_bit_writer *bw, uint8_t b)
{
    if (bw->obuf_pos >= sizeof(bw->obuf)) {
        if (!bz2_bw_flush_buffer(bw)) return false;
    }
    bw->obuf[bw->obuf_pos++] = b;
    return true;
}

bool bz2_bw_write_bits(bz2_bit_writer *bw, uint32_t val, int n)
{
    uint32_t mask = (n == 32) ? 0xFFFFFFFFu : (uint32_t)((1u << n) - 1u);
    bw->bits  = (bw->bits << n) | (uint64_t)(val & mask);
    bw->n_bits += n;
    while (bw->n_bits >= 8) {
        bw->n_bits -= 8;
        if (!bz2_bw_write_byte(bw, (uint8_t)(bw->bits >> bw->n_bits)))
            return false;
    }
    return true;
}

bool bz2_bw_flush(bz2_bit_writer *bw)
{
    /* Pad remaining bits */
    if (bw->n_bits > 0) {
        uint8_t b = (uint8_t)(bw->bits << (8 - bw->n_bits));
        bw->n_bits = 0;
        bw->bits   = 0;
        if (!bz2_bw_write_byte(bw, b)) return false;
    }
    return bz2_bw_flush_buffer(bw);
}

void bz2_bw_free(bz2_bit_writer *bw)
{
    (void)bw;
}

/* =========================================================================
 * Write decoded bytes to destination
 * ========================================================================= */

typedef struct {
    xx_io_device *dev;
    uint8_t      *mem;
    size_t        mem_cap;
    size_t        mem_written;
    uint8_t       obuf[65536];
    size_t        obuf_pos;
    bool          error;
    bool          overflow;
} bz2_out;

static bool bz2_out_flush(bz2_out *o)
{
    if (o->obuf_pos == 0) return true;
    if (o->dev) {
        ssize_t w = xx_io_write(o->dev, o->obuf, o->obuf_pos);
        if (w < 0 || (size_t)w != o->obuf_pos) { o->error = true; return false; }
    } else if (o->mem) {
        if (o->mem_written + o->obuf_pos > o->mem_cap) {
            size_t fit = o->mem_cap - o->mem_written;
            if (fit > 0) { xx_rt_memcpy(o->mem + o->mem_written, o->obuf, fit); o->mem_written += fit; }
            o->overflow = true; o->error = true; return false;
        }
        xx_rt_memcpy(o->mem + o->mem_written, o->obuf, o->obuf_pos);
        o->mem_written += o->obuf_pos;
    }
    o->obuf_pos = 0;
    return true;
}

static bool bz2_out_write(bz2_out *o, uint8_t b)
{
    if (o->obuf_pos >= sizeof(o->obuf)) {
        if (!bz2_out_flush(o)) return false;
    }
    o->obuf[o->obuf_pos++] = b;
    return true;
}

/* =========================================================================
 * Huffman decoder
 * ========================================================================= */

typedef struct {
    int      limit[BZ2_MAX_CODE_LEN + 1];
    int      low  [BZ2_MAX_CODE_LEN + 1];
    int      base [BZ2_MAX_CODE_LEN + 1];
    uint16_t perm [BZ2_MAX_ALPHA_SIZE];
    int      num_symbols;
    int      min_len;
    int      max_len;
} bz2_huffman;

static bool bz2_huff_build(bz2_huffman *ht, const uint8_t *lengths,
                           int n_syms)
{
    int count[BZ2_MAX_CODE_LEN + 1] = {0};
    if (!ht || !lengths || n_syms < 1 || n_syms > BZ2_MAX_ALPHA_SIZE) {
        return false;
    }
    xx_rt_memset(ht, 0, sizeof(*ht));
    ht->num_symbols = n_syms;
    for (int i = 0; i < n_syms; i++) {
        if (lengths[i] < 1 || lengths[i] > BZ2_MAX_CODE_LEN) {
            return false;
        }
        count[(int)lengths[i]]++;
    }
    count[0] = 0;

    ht->min_len = BZ2_MAX_CODE_LEN; ht->max_len = 0;
    for (int i = 0; i < n_syms; i++) {
        if (lengths[i] > 0 && lengths[i] < ht->min_len) ht->min_len = lengths[i];
        if (lengths[i] > ht->max_len) ht->max_len = lengths[i];
    }

    int first_index[BZ2_MAX_CODE_LEN + 1] = {0};
    int total = 0;
    for (int len = 1; len <= BZ2_MAX_CODE_LEN; len++) {
        first_index[len] = total;
        total += count[len];
    }

    int code = 0;
    for (int len = 1; len <= BZ2_MAX_CODE_LEN; len++) {
        code = (code + count[len - 1]) << 1;
        if (code > (1 << len) || count[len] > (1 << len) - code) {
            return false;
        }
        if (count[len] > 0) {
            ht->low[len] = code;
            ht->base[len] = code - first_index[len];
            ht->limit[len] = code + count[len] - 1;
        } else {
            ht->low[len] = 0;
            ht->base[len] = 0;
            ht->limit[len] = -1;
        }
    }

    /* Sort symbols by length into ht->perm */
    int idx[BZ2_MAX_CODE_LEN + 1];
    xx_rt_memcpy(idx, first_index, sizeof(idx));
    for (int i = 0; i < n_syms; i++) {
        if (lengths[i] > 0) ht->perm[idx[lengths[i]]++] = (uint16_t)i;
    }
    return true;
}

static int bz2_huff_decode(bz2_bit_reader *br, const bz2_huffman *ht)
{
    int v = 0;
    for (int len = 1; len <= ht->max_len; len++) {
        v = (v << 1) | (int)bz2_read_bit(br);
        if (br->error) return -1;
        if (ht->limit[len] >= 0 && v >= ht->low[len] &&
            v <= ht->limit[len]) {
            int index = v - ht->base[len];
            if (index < 0 || index >= ht->num_symbols) {
                return -1;
            }
            return (int)ht->perm[index];
        }
    }
    return -1; /* bad stream */
}

/* =========================================================================
 * Decompression of one bzip2 block
 * ========================================================================= */

static bool bz2_decompress_block(bz2_bit_reader *br, bz2_out *out,
                                 uint32_t *calculated_crc,
                                 xx_pd_struct *pd)
{
    (void)pd;

    if (!br || !out || !calculated_crc) return false;

    /* Block header: 6-byte magic already consumed by caller */
    uint32_t block_crc = bz2_read_bits(br, 32);
    int      rand_flag = (int)bz2_read_bit(br);
    uint32_t orig_ptr  = bz2_read_bits(br, 24);
    if (br->error) return false;

    /* Symbol map: which bytes appear in this block */
    uint16_t in_use_16 = (uint16_t)bz2_read_bits(br, 16);
    uint16_t sym_map[16];
    for (int hi = 0; hi < 16; hi++) {
        if (in_use_16 & (1u << (15 - hi))) {
            sym_map[hi] = (uint16_t)bz2_read_bits(br, 16);
        } else {
            sym_map[hi] = 0;
        }
    }
    if (br->error) return false;

    int n_syms = 0;
    uint16_t sym_to_byte[256];
    for (int hi = 0; hi < 16; hi++) {
        if (sym_map[hi]) {
            for (int lo = 0; lo < 16; lo++) {
                if (sym_map[hi] & (1u << (15 - lo))) {
                    sym_to_byte[n_syms++] = (uint16_t)(hi * 16 + lo);
                }
            }
        }
    }
    /* +2 for RUNA, RUNB */
    if (n_syms == 0) return false;
    int alpha_size = n_syms + 2;

    int n_groups   = (int)bz2_read_bits(br, 3);
    int n_selectors = (int)bz2_read_bits(br, 15);
    if (br->error || n_groups < 1 || n_groups > BZ2_N_GROUPS || n_selectors < 1)
        return false;

    /* Read selectors (MTF-coded) */
    uint8_t sel_mtf[BZ2_MAX_SELECTORS];
    uint8_t sel_list[BZ2_N_GROUPS];
    for (int i = 0; i < n_groups; i++) sel_list[i] = (uint8_t)i;
    for (int i = 0; i < n_selectors; i++) {
        int run = 0;
        while (bz2_read_bit(br)) {
            run++;
            if (run >= n_groups) return false;
        }
        if (br->error) return false;
        uint8_t tmp = sel_list[run];
        xx_rt_memmove(sel_list + 1, sel_list, (size_t)run);
        sel_list[0] = tmp;
        sel_mtf[i] = tmp;
    }

    /* Read Huffman code lengths and build tables */
    bz2_huffman htabs[BZ2_N_GROUPS];
    for (int g = 0; g < n_groups; g++) {
        uint8_t lengths[BZ2_MAX_ALPHA_SIZE];
        int curr = (int)bz2_read_bits(br, 5);
        if (br->error) return false;
        for (int i = 0; i < alpha_size; i++) {
            while (bz2_read_bit(br)) {
                if (br->error) return false;
                curr += bz2_read_bit(br) ? -1 : 1;
                if (br->error || curr < 1 || curr > BZ2_MAX_CODE_LEN) return false;
            }
            lengths[i] = (uint8_t)curr;
        }
        if (!bz2_huff_build(&htabs[g], lengths, alpha_size)) return false;
    }

    /* Decode MTF + RLE (first pass) → bwt_bytes[] array */
    uint8_t *bwt_bytes = (uint8_t *)xx_mem_alloc(BZ2_MAX_BLOCK_SIZE);
    if (!bwt_bytes) return false;

    uint32_t *tt = (uint32_t *)xx_mem_alloc(BZ2_MAX_BLOCK_SIZE * sizeof(uint32_t));
    if (!tt) { xx_mem_free(bwt_bytes); return false; }

    uint8_t mtf_vals[256];
    for (int i = 0; i < n_syms; i++) mtf_vals[i] = (uint8_t)i;

    int    sel_idx  = 0;
    int    group_sz = 0;
    int    cur_grp  = 0;
    int    tt_count = 0;

    /* Bijective base-2 RLE state */
    uint32_t run_count  = 0;
    uint32_t run_weight = 1;   /* bit-position weight: 1, 2, 4, 8, ... */
    bool     in_run     = false;

    while (1) {
        if (group_sz == 0) {
            if (sel_idx >= n_selectors) { xx_mem_free(bwt_bytes); xx_mem_free(tt); return false; }
            cur_grp = sel_mtf[sel_idx++];
            group_sz = 50;
        }
        group_sz--;

        int sym = bz2_huff_decode(br, &htabs[cur_grp]);
        if (br->error || sym < 0) { xx_mem_free(bwt_bytes); xx_mem_free(tt); return false; }

        if (sym == BZ2_RUNA || sym == BZ2_RUNB) {
            /* bijective base-2: RUNA contributes weight*1, RUNB contributes weight*2 */
            uint32_t multiplier = (uint32_t)(sym + 1);
            uint32_t contribution;
            if (run_weight > BZ2_MAX_BLOCK_SIZE / multiplier) {
                xx_mem_free(bwt_bytes);
                xx_mem_free(tt);
                return false;
            }
            contribution = run_weight * multiplier;
            if (run_count > BZ2_MAX_BLOCK_SIZE - contribution) {
                xx_mem_free(bwt_bytes);
                xx_mem_free(tt);
                return false;
            }
            run_count += contribution;
            run_weight = run_weight > BZ2_MAX_BLOCK_SIZE / 2U
                             ? BZ2_MAX_BLOCK_SIZE + 1U
                             : run_weight << 1;
            in_run = true;
            continue;
        }

        /* Flush any pending run of mtf_vals[0] */
        if (in_run) {
            uint8_t run_byte = (uint8_t)sym_to_byte[mtf_vals[0]];
            for (uint32_t k = 0; k < run_count; k++) {
                if (tt_count >= BZ2_MAX_BLOCK_SIZE) { xx_mem_free(bwt_bytes); xx_mem_free(tt); return false; }
                bwt_bytes[tt_count++] = run_byte;
            }
            in_run = false;
            run_count  = 0;
            run_weight = 1;
        }

        /* EOB? */
        if (sym == alpha_size - 1) break;

        /* MTF decode: sym 2..alpha_size-2 → MTF index sym-1 */
        int mtf_idx = sym - 1;
        uint8_t val = mtf_vals[mtf_idx];
        xx_rt_memmove(mtf_vals + 1, mtf_vals, (size_t)mtf_idx);
        mtf_vals[0] = val;
        uint8_t out_byte = (uint8_t)sym_to_byte[val];

        if (tt_count >= BZ2_MAX_BLOCK_SIZE) { xx_mem_free(bwt_bytes); xx_mem_free(tt); return false; }
        bwt_bytes[tt_count++] = out_byte;
    }

    /* Handle trailing run */
    if (in_run) {
        uint8_t run_byte = (uint8_t)sym_to_byte[mtf_vals[0]];
        for (uint32_t k = 0; k < run_count; k++) {
            if (tt_count >= BZ2_MAX_BLOCK_SIZE) { xx_mem_free(bwt_bytes); xx_mem_free(tt); return false; }
            bwt_bytes[tt_count++] = run_byte;
        }
    }

    /* BWT inversion: build inverse transform array */
    uint32_t freq[256] = {0};
    for (int i = 0; i < tt_count; i++) freq[bwt_bytes[i]]++;

    uint32_t cum[256] = {0};
    for (int i = 1; i < 256; i++) cum[i] = cum[i - 1] + freq[i - 1];

    uint32_t order[256];
    xx_rt_memcpy(order, cum, sizeof(cum));
    for (int i = 0; i < tt_count; i++) {
        uint8_t b = bwt_bytes[i];
        tt[order[b]++] = ((uint32_t)i << 8) | (uint32_t)b;
    }

    /* Output: follow BWT chain from orig_ptr and expand RLE2 */
    if (tt_count <= 0 || orig_ptr >= (uint32_t)tt_count) {
        xx_mem_free(bwt_bytes);
        xx_mem_free(tt);
        return false;
    }
    uint32_t out_crc = 0xFFFFFFFFUL;
    uint32_t t = tt[orig_ptr];
    unsigned prev_byte = 0x100;
    int rle_count = 0;

    for (int i = 0; i < tt_count; i++) {
        uint8_t ob = (uint8_t)(t & 0xFF);
        t = tt[t >> 8];

        if (rle_count == 4) {
            /* ob is repetition count (0..255) for prev_byte */
            for (uint32_t k = 0; k < (uint32_t)ob; k++) {
                out_crc = (out_crc << 8) ^ bz2_crc32_table[(out_crc >> 24) ^ (uint8_t)prev_byte];
                if (!bz2_out_write(out, (uint8_t)prev_byte)) {
                    xx_mem_free(bwt_bytes); xx_mem_free(tt); return false;
                }
            }
            rle_count = 0;
            prev_byte = 0x100;
        } else {
            if ((unsigned)ob == prev_byte) {
                rle_count++;
            } else {
                prev_byte = (unsigned)ob;
                rle_count = 1;
            }
            out_crc = (out_crc << 8) ^ bz2_crc32_table[(out_crc >> 24) ^ ob];
            if (!bz2_out_write(out, ob)) {
                xx_mem_free(bwt_bytes); xx_mem_free(tt); return false;
            }
        }
    }
    out_crc = ~out_crc;

    xx_mem_free(bwt_bytes);
    xx_mem_free(tt);

    if (rand_flag) {
        /* randomised block — rarely used; treat as unsupported for now */
        return false;
    }

    if (block_crc != out_crc || out->error) return false;
    *calculated_crc = out_crc;
    return true;
}

/* =========================================================================
 * Public decompression entry point
 * ========================================================================= */

bool xx_bzip2_decompress_stream(bz2_bit_reader *br,
                                xx_io_device *dst_dev,
                                uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd)
{
    bz2_out out;
    xx_rt_memset(&out, 0, sizeof(out));
    out.dev     = dst_dev;
    out.mem     = mem_dst;
    out.mem_cap = mem_cap;

    /* Stream header: "BZh" + digit (block-size) */
    uint8_t magic[4];
    magic[0] = (uint8_t)bz2_read_bits(br, 8);
    magic[1] = (uint8_t)bz2_read_bits(br, 8);
    magic[2] = (uint8_t)bz2_read_bits(br, 8);
    magic[3] = (uint8_t)bz2_read_bits(br, 8);
    if (br->error || magic[0] != 'B' || magic[1] != 'Z' || magic[2] != 'h' ||
        magic[3] < '1' || magic[3] > '9') {
        return false;
    }

    bool ok = true;
    bool saw_eos = false;
    uint32_t combined_crc = 0U;
    while (ok) {
        /* Read 48-bit block magic (or EOS magic) */
        uint32_t hi = bz2_read_bits(br, 32);
        uint16_t lo = (uint16_t)bz2_read_bits(br, 16);
        if (br->error) { ok = false; break; }

        if (hi == 0x31415926UL && lo == 0x5359U) {
            /* data block */
            uint32_t block_crc = 0U;
            ok = bz2_decompress_block(br, &out, &block_crc, pd);
            if (ok) {
                combined_crc = (combined_crc << 1) |
                               (combined_crc >> 31);
                combined_crc ^= block_crc;
            }
        } else if (hi == 0x17724538UL && lo == 0x5090U) {
            uint32_t stored_crc = bz2_read_bits(br, 32);
            if (br->error || stored_crc != combined_crc) {
                ok = false;
            } else {
                saw_eos = true;
            }
            break;
        } else {
            ok = false;
        }
    }

    /* Flush remaining output */
    if (ok && out.obuf_pos > 0) {
        if (out.dev) {
            ssize_t w = xx_io_write(out.dev, out.obuf, out.obuf_pos);
            if (w < 0 || (size_t)w != out.obuf_pos) ok = false;
        } else if (out.mem) {
            if (out.mem_written + out.obuf_pos <= out.mem_cap) {
                xx_rt_memcpy(out.mem + out.mem_written, out.obuf, out.obuf_pos);
                out.mem_written += out.obuf_pos;
            } else {
                ok = false;
            }
        }
    }

    if (out_written) *out_written = out.mem_written;
    return ok && saw_eos && !br->error && !out.error;
}
