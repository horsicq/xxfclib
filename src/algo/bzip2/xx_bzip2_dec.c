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
#include "xx_bzip2_random.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "asm/xx_bzip2_asm.h"
#include "../../io/xx_io_buffer.h"
#include <string.h>

extern uint32_t xx_crc32_mpeg2_raw(uint32_t reg, const void *data, size_t size);

/* =========================================================================
 * Bit-reader helpers
 * ========================================================================= */

static bool bz2_br_refill(bz2_bit_reader *br)
{
    if (br->error || br->eof) return false;
    if (br->ibuf_pos < br->ibuf_len) return true;
    if (!br->dev) {
        br->eof = true;
        return false;
    }

    size_t want = br->ibuf_capacity;
    if (br->remaining >= 0) {
        if (br->remaining == 0) {
            br->eof = true;
            return false;
        }
        if ((int64_t)want > br->remaining) want = (size_t)br->remaining;
    }

    ssize_t got = xx_io_read(br->dev, br->ibuf, want);
    if (got <= 0) {
        br->eof = true;
        return false;
    }
    if (br->remaining >= 0) br->remaining -= got;
    br->ibuf_pos = 0;
    br->ibuf_len = (size_t)got;
    return true;
}

bool bz2_br_init(bz2_bit_reader *br, xx_io_device *dev, const uint8_t *mem, size_t mem_size, int64_t remaining)
{
    xx_rt_memset(br, 0, sizeof(*br));
    br->dev = dev;
    br->mem = mem;
    br->mem_size = mem_size;
    br->remaining = remaining;
    if (!dev && mem) {
        br->ibuf = (uint8_t *)mem;
        br->ibuf_capacity = mem_size;
        br->ibuf_pos = 0;
        br->ibuf_len = (remaining >= 0 && (size_t)remaining < mem_size) ? (size_t)remaining : mem_size;
        br->mem_pos = br->ibuf_len;
        br->error = false;
    } else {
        br->ibuf_capacity = xx_get_file_buffer_size();
        if (br->ibuf_capacity < (256 * 1024)) br->ibuf_capacity = (256 * 1024);
        if (br->ibuf_capacity > (SIZE_MAX >> 1)) br->ibuf_capacity = SIZE_MAX >> 1;
        /* bz2_br_refill never reads more than remains of a sized stream. */
        if (remaining >= 0) br->ibuf_capacity = xx_io_buffer_size_for(br->ibuf_capacity, (uint64_t)remaining);
        br->ibuf = (uint8_t *)xx_mem_alloc(br->ibuf_capacity);
        br->error = br->ibuf == NULL;
        if (br->error) xx_io_note_alloc_failure();
    }
    return !br->error;
}

void bz2_br_free(bz2_bit_reader *br)
{
    if (br && br->dev && br->ibuf) {
        xx_mem_free(br->ibuf);
        br->ibuf = NULL;
    }
}

static inline void bz2_ensure_bits(bz2_bit_reader *br, int n)
{
    while (br->n_bits < n) {
        if (br->ibuf_pos >= br->ibuf_len) {
            if (!bz2_br_refill(br)) return;
        }
        if (br->ibuf_len - br->ibuf_pos >= 4 && br->n_bits <= 32) {
            uint32_t w = ((uint32_t)br->ibuf[br->ibuf_pos] << 24) |
                         ((uint32_t)br->ibuf[br->ibuf_pos + 1] << 16) |
                         ((uint32_t)br->ibuf[br->ibuf_pos + 2] << 8) |
                         ((uint32_t)br->ibuf[br->ibuf_pos + 3]);
            br->bits = (br->bits << 32) | (uint64_t)w;
            br->ibuf_pos += 4;
            br->n_bits += 32;
        } else {
            br->bits = (br->bits << 8) | (uint64_t)br->ibuf[br->ibuf_pos++];
            br->n_bits += 8;
        }
    }
}

static inline uint32_t bz2_read_bits(bz2_bit_reader *br, int n)
{
    if (br->n_bits < n) {
        bz2_ensure_bits(br, n);
        if (br->n_bits < n) {
            br->error = true;
            return 0;
        }
    }
    br->n_bits -= n;
    uint32_t mask = (n == 32) ? 0xFFFFFFFFu : (uint32_t)((1u << n) - 1u);
    return (uint32_t)(br->bits >> br->n_bits) & mask;
}

static inline uint32_t bz2_read_bit(bz2_bit_reader *br)
{
    return bz2_read_bits(br, 1);
}

/* =========================================================================
 * Bit-writer helpers
 * ========================================================================= */

bool bz2_bw_init(bz2_bit_writer *bw, xx_io_device *dev, uint8_t *mem, size_t mem_cap)
{
    xx_rt_memset(bw, 0, sizeof(*bw));
    bw->dev = dev;
    bw->mem = mem;
    bw->mem_cap = mem_cap;
    bw->obuf_capacity = xx_get_file_buffer_size();
    if (bw->obuf_capacity > (SIZE_MAX >> 1)) bw->obuf_capacity = SIZE_MAX >> 1;
    bw->obuf = (uint8_t *)xx_mem_alloc(bw->obuf_capacity);
    bw->error = bw->obuf == NULL;
    return !bw->error;
}

static bool bz2_bw_flush_buffer(bz2_bit_writer *bw)
{
    if (bw->obuf_pos > 0) {
        if (bw->dev) {
            ssize_t w = xx_io_write(bw->dev, bw->obuf, bw->obuf_pos);
            if (w < 0 || (size_t)w != bw->obuf_pos) {
                bw->error = true;
                return false;
            }
        } else if (bw->mem) {
            if (bw->mem_pos + bw->obuf_pos > bw->mem_cap) {
                bw->error = true;
                return false;
            }
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
    if (bw->error) return false;
    if (bw->obuf_pos >= bw->obuf_capacity) {
        if (!bz2_bw_flush_buffer(bw)) return false;
    }
    bw->obuf[bw->obuf_pos++] = b;
    return true;
}

bool bz2_bw_write_bits(bz2_bit_writer *bw, uint32_t val, int n)
{
    uint32_t mask = (n == 32) ? 0xFFFFFFFFu : (uint32_t)((1u << n) - 1u);
    bw->bits = (bw->bits << n) | (uint64_t)(val & mask);
    bw->n_bits += n;
    while (bw->n_bits >= 8) {
        bw->n_bits -= 8;
        if (!bz2_bw_write_byte(bw, (uint8_t)(bw->bits >> bw->n_bits))) return false;
    }
    return true;
}

bool bz2_bw_flush(bz2_bit_writer *bw)
{
    /* Pad remaining bits */
    if (bw->n_bits > 0) {
        uint8_t b = (uint8_t)(bw->bits << (8 - bw->n_bits));
        bw->n_bits = 0;
        bw->bits = 0;
        if (!bz2_bw_write_byte(bw, b)) return false;
    }
    return bz2_bw_flush_buffer(bw);
}

void bz2_bw_free(bz2_bit_writer *bw)
{
    if (bw) {
        xx_mem_free(bw->obuf);
        bw->obuf = NULL;
    }
}

/* =========================================================================
 * Write decoded bytes to destination
 * ========================================================================= */

typedef struct {
    xx_io_device *dev;
    uint8_t *mem;
    size_t mem_cap;
    size_t mem_written;
    uint8_t *obuf;
    size_t obuf_capacity;
    size_t obuf_pos;
    uint32_t block_crc;
    bool block_crc_active;
    bool error;
    bool overflow;
    bool allocated_obuf;
    size_t obuf_limit; /* growth bound of an allocated obuf */
} bz2_out;

static bool bz2_out_flush(bz2_out *o)
{
    if (o->obuf_pos == 0) return true;
    if (o->dev) {
        ssize_t w = xx_io_write(o->dev, o->obuf, o->obuf_pos);
        if (w < 0 || (size_t)w != o->obuf_pos) {
            o->error = true;
            return false;
        }
        if (o->block_crc_active) {
            o->block_crc = xx_crc32_mpeg2_raw(o->block_crc, o->obuf, o->obuf_pos);
        }
        /* The output size is unknown: a full staging buffer doubles, up to
         * the configured size. Growth is optional; on failure the current
         * buffer stays in use. */
        if (o->allocated_obuf && o->obuf_pos == o->obuf_capacity && o->obuf_capacity < o->obuf_limit) {
            size_t grown = o->obuf_capacity <= o->obuf_limit / 2U ? o->obuf_capacity * 2U : o->obuf_limit;
            uint8_t *larger = (uint8_t *)xx_mem_alloc(grown);
            if (larger) {
                xx_mem_free(o->obuf);
                o->obuf = larger;
                o->obuf_capacity = grown;
            }
        }
        o->obuf_pos = 0;
    } else if (o->mem) {
        o->mem_written = o->obuf_pos;
    }
    return true;
}

static inline bool bz2_out_write(bz2_out *o, uint8_t b)
{
    if (o->obuf_pos >= o->obuf_capacity) {
        if (o->mem) {
            o->overflow = true;
            o->error = true;
            return false;
        }
        if (!bz2_out_flush(o)) return false;
    }
    o->obuf[o->obuf_pos++] = b;
    return true;
}

static inline bool bz2_out_write_repeat(bz2_out *o, uint8_t b, size_t count)
{
    if (count == 0) return true;
    if (o->mem) {
        if (o->obuf_pos + count > o->obuf_capacity) {
            o->overflow = true;
            o->error = true;
            return false;
        }
        xx_rt_memset(o->obuf + o->obuf_pos, b, count);
        o->obuf_pos += count;
        return true;
    }
    while (count != 0) {
        size_t available = o->obuf_capacity - o->obuf_pos;
        if (available == 0) {
            if (!bz2_out_flush(o)) return false;
            available = o->obuf_capacity;
        }
        size_t chunk = count < available ? count : available;
        xx_rt_memset(o->obuf + o->obuf_pos, b, chunk);
        o->obuf_pos += chunk;
        count -= chunk;
    }
    return true;
}

/* =========================================================================
 * Huffman decoder
 * ========================================================================= */

#define BZ2_HUFF_FAST_BITS 11
#define BZ2_HUFF_FAST_SIZE (1 << BZ2_HUFF_FAST_BITS)
#define BZ2_HUFF_FAST_MASK (BZ2_HUFF_FAST_SIZE - 1)

typedef struct {
    int limit[BZ2_MAX_CODE_LEN + 1];
    int low[BZ2_MAX_CODE_LEN + 1];
    int base[BZ2_MAX_CODE_LEN + 1];
    uint16_t perm[BZ2_MAX_ALPHA_SIZE];
    int16_t fast_table[BZ2_HUFF_FAST_SIZE];
    int num_symbols;
    int min_len;
    int max_len;
} bz2_huffman;

static bool bz2_huff_build(bz2_huffman *ht, const uint8_t *lengths, int n_syms)
{
    int count[BZ2_MAX_CODE_LEN + 1] = {0};
    if (!ht || !lengths || n_syms < 1 || n_syms > BZ2_MAX_ALPHA_SIZE) {
        return false;
    }
    xx_rt_memset(ht, 0, sizeof(*ht));
    xx_rt_memset(ht->fast_table, 0xFF, sizeof(ht->fast_table));
    ht->num_symbols = n_syms;
    for (int i = 0; i < n_syms; i++) {
        if (lengths[i] < 1 || lengths[i] > BZ2_MAX_CODE_LEN) {
            return false;
        }
        count[(int)lengths[i]]++;
    }
    count[0] = 0;

    ht->min_len = BZ2_MAX_CODE_LEN;
    ht->max_len = 0;
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

    /* Build fast lookup table */
    for (int len = 1; len <= BZ2_HUFF_FAST_BITS; len++) {
        if (ht->limit[len] >= 0) {
            int shift = BZ2_HUFF_FAST_BITS - len;
            int num_pads = 1 << shift;
            for (int c = ht->low[len]; c <= ht->limit[len]; c++) {
                int sym = (int)ht->perm[c - ht->base[len]];
                int entry = (sym << 5) | len;
                int base_key = c << shift;
                for (int pad = 0; pad < num_pads; pad++) {
                    ht->fast_table[base_key | pad] = (int16_t)entry;
                }
            }
        }
    }
    return true;
}

static inline int bz2_huff_decode(bz2_bit_reader *br, const bz2_huffman *ht)
{
    if (br->n_bits < BZ2_HUFF_FAST_BITS) {
        bz2_ensure_bits(br, 24);
    }
    if (br->n_bits >= BZ2_HUFF_FAST_BITS) {
        uint32_t peek = (uint32_t)(br->bits >> (br->n_bits - BZ2_HUFF_FAST_BITS)) & BZ2_HUFF_FAST_MASK;
        int16_t entry = ht->fast_table[peek];
        if (entry >= 0) {
            int len = entry & 0x1F;
            br->n_bits -= len;
            return entry >> 5;
        }
    }
    int v = 0;
    for (int len = 1; len <= ht->max_len; len++) {
        if (br->n_bits < 1) {
            bz2_ensure_bits(br, 1);
            if (br->n_bits < 1) {
                br->error = true;
                return -1;
            }
        }
        --br->n_bits;
        int bit = (int)((br->bits >> br->n_bits) & 1u);
        v = (v << 1) | bit;
        if (ht->limit[len] >= 0 && v >= ht->low[len] && v <= ht->limit[len]) {
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

static bool bz2_decompress_block(bz2_bit_reader *br, bz2_out *out, int block_bytes, uint8_t *bwt_bytes, uint32_t *tt, uint32_t *calculated_crc, xx_pd_struct *pd)
{
    (void)pd;

    if (!br || !out || !calculated_crc || !bwt_bytes || !tt || block_bytes < 100000 || block_bytes > BZ2_MAX_BLOCK_SIZE) return false;

    /* Block header: 6-byte magic already consumed by caller */
    uint32_t block_crc = bz2_read_bits(br, 32);
    int rand_flag = (int)bz2_read_bit(br);
    uint32_t orig_ptr = bz2_read_bits(br, 24);
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

    int n_groups = (int)bz2_read_bits(br, 3);
    int n_selectors = (int)bz2_read_bits(br, 15);
    if (br->error || n_groups < 1 || n_groups > BZ2_N_GROUPS || n_selectors < 1) return false;

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

    uint8_t mtf_vals[256];
    for (int i = 0; i < n_syms; i++) mtf_vals[i] = (uint8_t)i;

    int sel_idx = 0;
    int group_sz = 0;
    const bz2_huffman *cur_ht = &htabs[0];
    int tt_count = 0;

    /* Bijective base-2 RLE state */
    uint32_t run_count = 0;
    uint32_t run_weight = 1; /* bit-position weight: 1, 2, 4, 8, ... */
    bool in_run = false;

    uint32_t freq[256] = {0};

    while (1) {
        if (group_sz == 0) {
            if (sel_idx >= n_selectors) return false;
            cur_ht = &htabs[sel_mtf[sel_idx++]];
            group_sz = 50;
        }
        group_sz--;

        int sym = bz2_huff_decode(br, cur_ht);
        if (br->error || sym < 0) return false;

        if (sym == BZ2_RUNA || sym == BZ2_RUNB) {
            /* bijective base-2: RUNA contributes weight*1, RUNB contributes weight*2 */
            uint32_t multiplier = (uint32_t)(sym + 1);
            uint32_t available = (uint32_t)(block_bytes - tt_count);
            uint32_t contribution;
            if (run_weight > available / multiplier) return false;
            contribution = run_weight * multiplier;
            if (run_count > available - contribution) return false;
            run_count += contribution;
            run_weight = run_weight > (uint32_t)block_bytes / 2U ? (uint32_t)block_bytes + 1U : run_weight << 1;
            in_run = true;
            continue;
        }

        /* Flush any pending run of mtf_vals[0] */
        if (in_run) {
            uint8_t run_byte = (uint8_t)sym_to_byte[mtf_vals[0]];
            if (run_count > (uint32_t)(block_bytes - tt_count)) return false;
            xx_rt_memset(bwt_bytes + tt_count, run_byte, run_count);
            freq[run_byte] += run_count;
            tt_count += (int)run_count;
            in_run = false;
            run_count = 0;
            run_weight = 1;
        }

        /* EOB? */
        if (sym == alpha_size - 1) break;

        /* MTF decode: sym 2..alpha_size-2 → MTF index sym-1 */
        int mtf_idx = sym - 1;
        uint8_t val = mtf_vals[mtf_idx];
        if (mtf_idx == 0) {
            /* already at front */
        } else if (mtf_idx <= 7) {
            for (int k = mtf_idx; k > 0; k--) {
                mtf_vals[k] = mtf_vals[k - 1];
            }
            mtf_vals[0] = val;
        } else {
            xx_rt_memmove(mtf_vals + 1, mtf_vals, (size_t)mtf_idx);
            mtf_vals[0] = val;
        }
        uint8_t out_byte = (uint8_t)sym_to_byte[val];

        if (tt_count >= block_bytes) return false;
        bwt_bytes[tt_count++] = out_byte;
        freq[out_byte]++;
    }

    /* Handle trailing run */
    if (in_run) {
        uint8_t run_byte = (uint8_t)sym_to_byte[mtf_vals[0]];
        if (run_count > (uint32_t)(block_bytes - tt_count)) return false;
        xx_rt_memset(bwt_bytes + tt_count, run_byte, run_count);
        freq[run_byte] += run_count;
        tt_count += (int)run_count;
    }

    /* BWT inversion: build inverse transform array */
#if XX_BZIP2_HAS_ASM
#if defined(_M_X64)
    xx_bzip2_bwt_build_x64(bwt_bytes, tt, freq, tt_count);
#elif defined(_M_IX86)
    xx_bzip2_bwt_build_x86(bwt_bytes, tt, freq, tt_count);
#endif
#else
    uint32_t cum[256] = {0};
    for (int i = 1; i < 256; i++) cum[i] = cum[i - 1] + freq[i - 1];

    for (int i = 0; i < tt_count; i++) {
        uint8_t b = bwt_bytes[i];
        tt[cum[b]++] = ((uint32_t)i << 8) | (uint32_t)b;
    }
#endif

    /* Output: follow BWT chain from orig_ptr and expand RLE2 */
    if (tt_count <= 0 || orig_ptr >= (uint32_t)tt_count) return false;
    uint32_t t = tt[orig_ptr];
    unsigned prev_byte = 0x100;
    int rle_count = 0;

    size_t block_start_pos = out->obuf_pos;
    if (out->dev != NULL) {
        out->block_crc = 0xFFFFFFFFU;
        out->block_crc_active = true;
    }

    if (!rand_flag && out->mem) {
#if XX_BZIP2_HAS_ASM
        size_t written = 0;
#if defined(_M_X64)
        bool ok = xx_bzip2_bwt_traverse_x64(tt, tt_count, orig_ptr, out->obuf + out->obuf_pos, out->obuf_capacity - out->obuf_pos, &written);
#elif defined(_M_IX86)
        bool ok = xx_bzip2_bwt_traverse_x86(tt, tt_count, orig_ptr, out->obuf + out->obuf_pos, out->obuf_capacity - out->obuf_pos, &written);
#endif
        if (!ok) {
            out->error = true;
            return false;
        }
        out->obuf_pos += written;
#else
        uint8_t *dest = out->obuf + out->obuf_pos;
        uint8_t *dest_end = out->obuf + out->obuf_capacity;
        for (int i = 0; i < tt_count; i++) {
            uint8_t ob = (uint8_t)(t & 0xFF);
            t = tt[t >> 8];

            if (rle_count == 4) {
                if (dest + ob > dest_end) { out->error = true; return false; }
                xx_rt_memset(dest, (uint8_t)prev_byte, ob);
                dest += ob;
                rle_count = 0;
                prev_byte = 0x100;
            } else {
                if ((unsigned)ob == prev_byte) {
                    rle_count++;
                } else {
                    prev_byte = (unsigned)ob;
                    rle_count = 1;
                }
                if (dest >= dest_end) { out->error = true; return false; }
                *dest++ = ob;
            }
        }
        out->obuf_pos = (size_t)(dest - out->obuf);
#endif
    } else if (!rand_flag) {
        for (int i = 0; i < tt_count; i++) {
            uint8_t ob = (uint8_t)(t & 0xFF);
            t = tt[t >> 8];

            if (rle_count == 4) {
                if (!bz2_out_write_repeat(out, (uint8_t)prev_byte, ob)) return false;
                rle_count = 0;
                prev_byte = 0x100;
            } else {
                if ((unsigned)ob == prev_byte) {
                    rle_count++;
                } else {
                    prev_byte = (unsigned)ob;
                    rle_count = 1;
                }
                if (!bz2_out_write(out, ob)) return false;
            }
        }
    } else {
        unsigned random_left = 0U;
        unsigned random_index = 0U;
        for (int i = 0; i < tt_count; i++) {
            uint8_t ob = (uint8_t)(t & 0xFF);
            t = tt[t >> 8];
            if (random_left == 0U) {
                random_left = bz2_random_numbers[random_index];
                random_index = (random_index + 1U) & 511U;
            }
            --random_left;
            if (random_left == 1U) ob ^= 1U;

            if (rle_count == 4) {
                if (!bz2_out_write_repeat(out, (uint8_t)prev_byte, ob)) return false;
                rle_count = 0;
                prev_byte = 0x100;
            } else {
                if ((unsigned)ob == prev_byte) {
                    rle_count++;
                } else {
                    prev_byte = (unsigned)ob;
                    rle_count = 1;
                }
                if (!bz2_out_write(out, ob)) return false;
            }
        }
    }

    uint32_t out_crc;
    if (out->dev != NULL) {
        if (!bz2_out_flush(out)) return false;
        out_crc = out->block_crc ^ 0xFFFFFFFFU;
        out->block_crc_active = false;
    } else {
        size_t block_len = out->obuf_pos - block_start_pos;
        out_crc = xx_crc32(XX_CRC_TYPE_CRC32_BZIP2, out->obuf + block_start_pos, block_len);
    }

    if (block_crc != out_crc || out->error) return false;
    *calculated_crc = out_crc;
    return true;
}

/* =========================================================================
 * Public decompression entry point
 * ========================================================================= */

bool xx_bzip2_decompress_stream(bz2_bit_reader *br, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written, xx_pd_struct *pd)
{
    bz2_out out;
    xx_rt_memset(&out, 0, sizeof(out));
    out.dev = dst_dev;
    out.mem = mem_dst;
    out.mem_cap = mem_cap;
    if (!br || br->error) return false;

    /* Stream header: "BZh" + digit (block-size) */
    uint8_t magic[4];
    magic[0] = (uint8_t)bz2_read_bits(br, 8);
    magic[1] = (uint8_t)bz2_read_bits(br, 8);
    magic[2] = (uint8_t)bz2_read_bits(br, 8);
    magic[3] = (uint8_t)bz2_read_bits(br, 8);
    if (br->error || magic[0] != 'B' || magic[1] != 'Z' || magic[2] != 'h' || magic[3] < '1' || magic[3] > '9') {
        return false;
    }
    /* The header limits the block after the first RLE stage, not the number
     * of bytes ultimately emitted. Reject oversized blocks before BWT inversion. */
    int block_bytes = (int)(magic[3] - '0') * 100000;

    if (mem_dst) {
        out.obuf = mem_dst;
        out.obuf_capacity = mem_cap;
        out.allocated_obuf = false;
    } else {
        /* As the reader's buffer before it was sized to the input: the
         * configured size, at least 256 KiB, reached by growth. */
        out.obuf_limit = xx_get_file_buffer_size();
        if (out.obuf_limit < (256 * 1024)) out.obuf_limit = (256 * 1024);
        if (out.obuf_limit > (SIZE_MAX >> 1)) out.obuf_limit = SIZE_MAX >> 1;
        out.obuf_capacity = out.obuf_limit < (256 * 1024) ? out.obuf_limit : (256 * 1024);
        out.obuf = (uint8_t *)xx_mem_alloc(out.obuf_capacity);
        if (!out.obuf) {
            xx_io_note_alloc_failure();
            return false;
        }
        out.allocated_obuf = true;
    }

    /* Allocate scratch buffers once for the entire stream to avoid per-block heap allocation. */
    uint8_t *bwt_bytes = (uint8_t *)xx_mem_alloc((size_t)block_bytes);
    uint32_t *tt = (uint32_t *)xx_mem_alloc((size_t)block_bytes * sizeof(uint32_t));
    if (!bwt_bytes || !tt) {
        xx_io_note_alloc_failure();
        if (bwt_bytes) xx_mem_free(bwt_bytes);
        if (tt) xx_mem_free(tt);
        if (out.allocated_obuf && out.obuf) xx_mem_free(out.obuf);
        return false;
    }

    bool ok = true;
    bool saw_eos = false;
    uint32_t combined_crc = 0U;
    while (ok) {
        /* A block is at most 900 kB of output: a stop request is seen
         * between blocks. */
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        /* Read 48-bit block magic (or EOS magic) */
        uint32_t hi = bz2_read_bits(br, 32);
        uint16_t lo = (uint16_t)bz2_read_bits(br, 16);
        if (br->error) {
            ok = false;
            break;
        }

        if (hi == 0x31415926UL && lo == 0x5359U) {
            /* data block */
            uint32_t block_crc = 0U;
            ok = bz2_decompress_block(br, &out, block_bytes, bwt_bytes, tt, &block_crc, pd);
            if (ok) {
                combined_crc = (combined_crc << 1) | (combined_crc >> 31);
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

    if (ok && !bz2_out_flush(&out)) ok = false;

    if (out_written) *out_written = out.obuf_pos;
    xx_mem_free(bwt_bytes);
    xx_mem_free(tt);
    if (out.allocated_obuf && out.obuf) xx_mem_free(out.obuf);
    return ok && saw_eos && !br->error && !out.error;
}
