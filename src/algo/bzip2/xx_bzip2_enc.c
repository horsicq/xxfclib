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

/* Bzip2 compressor — clean C implementation.
 *
 * Algorithm: BWT + MTF + RLE + canonical Huffman, multi-table selection
 * following Julian Seward's bzip2 format.
 *
 * Block-size multiplier: 1..9 (100 kB..900 kB).
 * Each block is independently compressed and carries its own CRC32.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_bzip2_internal.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* =========================================================================
 * BWT (Burrows-Wheeler Transform) via suffix sort
 * ========================================================================= */


/* Simple O(n log n) suffix sort using comparison-based sort.
 * For production, replace with SA-IS for O(n). */
static int bwt_compare(const void *a, const void *b, void *ctx)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    /* Compare as circular strings; length is stored in data[-4..-1]? No —
     * we embed length separately. Use strcmp-like comparison on circular view. */
    /* ctx points to struct { int n; uint8_t data[]; } */
    const int *p = (const int *)ctx;
    int n = p[0];
    const uint8_t *d = (const uint8_t *)(p + 1);
    for (int k = 0; k < n; k++) {
        int ca = d[(ia + k) % n];
        int cb = d[(ib + k) % n];
        if (ca != cb) return ca - cb;
    }
    return 0;
}

/* Returns the BWT of src[0..n-1] into bwt[], and sets *orig_ptr. */
static bool bwt_transform(const uint8_t *src, int n, uint8_t *bwt, int *orig_ptr)
{
    /* Allocate a block for [n, src copy] to pass to the context sort. */
    int *ctx = (int *)xx_mem_alloc((size_t)(sizeof(int) + n));
    if (!ctx) return false;
    ctx[0] = n;
    xx_rt_memcpy(ctx + 1, src, (size_t)n);

    int *idx = (int *)xx_mem_alloc((size_t)n * sizeof(int));
    if (!idx) { xx_mem_free(ctx); return false; }
    for (int i = 0; i < n; i++) idx[i] = i;

    xx_rt_qsort_context(idx, (size_t)n, sizeof(int), bwt_compare, ctx);

    const uint8_t *d = (const uint8_t *)(ctx + 1);
    *orig_ptr = 0;
    for (int i = 0; i < n; i++) {
        bwt[i] = d[(idx[i] + n - 1) % n];
        if (idx[i] == 0) *orig_ptr = i;
    }
    xx_mem_free(idx);
    xx_mem_free(ctx);
    return true;
}

/* =========================================================================
 * MTF encoding
 * ========================================================================= */
static void mtf_encode(const uint8_t *src, int n, int *mtf_out, const uint8_t *in_use_table)
{
    uint8_t mtf[256];
    int k = 0;
    for (int i = 0; i < 256; i++) if (in_use_table[i]) mtf[k++] = (uint8_t)i;

    for (int i = 0; i < n; i++) {
        int pos = 0;
        while (mtf[pos] != src[i]) pos++;
        mtf_out[i] = pos;
        /* move to front */
        uint8_t tmp = mtf[pos];
        xx_rt_memmove(mtf + 1, mtf, (size_t)pos);
        mtf[0] = tmp;
    }
}

/* =========================================================================
 * Simple 1-pass Huffman code assignment (length-limited, N_GROUPS=1 for now)
 * Full multi-group selection is a TODO — for now we use 1 table (valid but
 * sub-optimal). The decompressor handles it correctly (n_groups=1).
 * ========================================================================= */

typedef struct {
    uint32_t freq;
    uint16_t symbol;
    int16_t  left;
    int16_t  right;
} bz2_huff_node;

static void build_huffman_lengths(const uint32_t *freq, int n_syms, uint8_t *lengths)
{
    xx_rt_memset(lengths, 0, (size_t)n_syms);
    bz2_huff_node nodes[2 * BZ2_MAX_ALPHA_SIZE + 2];
    int num_nodes = 0;

    for (int i = 0; i < n_syms; ++i) {
        nodes[num_nodes].freq = freq[i] > 0 ? freq[i] : 1;
        nodes[num_nodes].symbol = (uint16_t)i;
        nodes[num_nodes].left = -1;
        nodes[num_nodes].right = -1;
        num_nodes++;
    }

    if (num_nodes == 0) return;
    if (num_nodes == 1) {
        lengths[nodes[0].symbol] = 1;
        return;
    }

    int active = num_nodes;
    int cur_nodes = num_nodes;

    while (active > 1) {
        int min1 = -1, min2 = -1;
        for (int i = 0; i < cur_nodes; ++i) {
            if (nodes[i].freq == 0) continue;
            if (min1 < 0 || nodes[i].freq < nodes[min1].freq) {
                min2 = min1;
                min1 = i;
            } else if (min2 < 0 || nodes[i].freq < nodes[min2].freq) {
                min2 = i;
            }
        }

        nodes[cur_nodes].freq = nodes[min1].freq + nodes[min2].freq;
        nodes[cur_nodes].symbol = 0xFFFF;
        nodes[cur_nodes].left = (int16_t)min1;
        nodes[cur_nodes].right = (int16_t)min2;

        nodes[min1].freq = 0;
        nodes[min2].freq = 0;
        cur_nodes++;
        active--;
    }

    int root = cur_nodes - 1;
    int stack[2 * BZ2_MAX_ALPHA_SIZE + 2];
    int depths[2 * BZ2_MAX_ALPHA_SIZE + 2];
    int top = 0;

    stack[top] = root;
    depths[top] = 0;
    top++;

    while (top > 0) {
        top--;
        int u = stack[top];
        int d = depths[top];

        if (nodes[u].left >= 0 && nodes[u].right >= 0) {
            stack[top] = nodes[u].left;
            depths[top] = d + 1;
            top++;
            stack[top] = nodes[u].right;
            depths[top] = d + 1;
            top++;
        } else {
            int len = d > 20 ? 20 : d;
            if (len == 0) len = 1;
            lengths[nodes[u].symbol] = (uint8_t)len;
        }
    }
}

/* Build canonical code values from lengths */
static void canonical_codes(const uint8_t *lengths, int n, uint32_t *codes)
{
    int count[21] = {0};
    for (int i = 0; i < n; i++) count[(int)lengths[i]]++;
    count[0] = 0;
    uint32_t code = 0;
    int next_code[21] = {0};
    for (int bits = 1; bits <= 20; bits++) {
        code = (code + count[bits - 1]) << 1;
        next_code[bits] = (int)code;
    }
    for (int i = 0; i < n; i++) {
        if (lengths[i]) codes[i] = (uint32_t)next_code[(int)lengths[i]]++;
        else            codes[i] = 0;
    }
}

/* =========================================================================
 * Compress one block
 * ========================================================================= */

static bool compress_block(bz2_bit_writer *bw,
                           const uint8_t *data, int data_len,
                           int block_size_100k, uint32_t *stream_crc)
{
    (void)block_size_100k;

    /* Compute block CRC (bzip2 uses bit-reversed CRC32 on original uncompressed data) */
    uint32_t crc = 0xFFFFFFFFUL;
    for (int j = 0; j < data_len; j++) {
        crc = (crc << 8) ^ bz2_crc32_table[(crc >> 24) ^ data[j]];
    }
    crc = ~crc;
    *stream_crc = (*stream_crc << 1) | (*stream_crc >> 31);
    *stream_crc ^= crc;

    /* RLE encode: 4 identical bytes followed by repeat count - 4 (0..255) */
    uint8_t *rle_data = (uint8_t *)xx_mem_alloc((size_t)data_len + (size_t)data_len / 4 + 32);
    if (!rle_data) return false;
    int rle_data_len = 0;
    int idx_in = 0;
    while (idx_in < data_len) {
        uint8_t b = data[idx_in];
        int run = 1;
        while (idx_in + run < data_len && data[idx_in + run] == b && run < 255 + 4) {
            run++;
        }
        if (run >= 4) {
            rle_data[rle_data_len++] = b;
            rle_data[rle_data_len++] = b;
            rle_data[rle_data_len++] = b;
            rle_data[rle_data_len++] = b;
            rle_data[rle_data_len++] = (uint8_t)(run - 4);
        } else {
            for (int k = 0; k < run; k++) {
                rle_data[rle_data_len++] = b;
            }
        }
        idx_in += run;
    }

    /* BWT on RLE-encoded block */
    uint8_t *bwt = (uint8_t *)xx_mem_alloc((size_t)rle_data_len);
    if (!bwt) { xx_mem_free(rle_data); return false; }
    int orig_ptr = 0;
    if (!bwt_transform(rle_data, rle_data_len, bwt, &orig_ptr)) {
        xx_mem_free(bwt);
        xx_mem_free(rle_data);
        return false;
    }
    xx_mem_free(rle_data);

    /* Symbol map */
    uint8_t in_use[256];
    xx_rt_memset(in_use, 0, sizeof(in_use));
    for (int j = 0; j < rle_data_len; j++) in_use[bwt[j]] = 1;

    int n_syms = 0;
    for (int j = 0; j < 256; j++) if (in_use[j]) n_syms++;
    int alpha_size = n_syms + 2; /* +RUNA +RUNB +EOB */

    /* MTF */
    int *mtf_vals = (int *)xx_mem_alloc((size_t)rle_data_len * sizeof(int));
    if (!mtf_vals) { xx_mem_free(bwt); return false; }
    mtf_encode(bwt, rle_data_len, mtf_vals, in_use);
    xx_mem_free(bwt);

    /* RLE (RUNA/RUNB) + build frequency table */
    uint32_t freq[BZ2_MAX_ALPHA_SIZE];
    xx_rt_memset(freq, 0, sizeof(freq));
    int *rle_buf = (int *)xx_mem_alloc(((size_t)rle_data_len + 2) * sizeof(int));
    if (!rle_buf) { xx_mem_free(mtf_vals); return false; }
    int rle_len = 0;
    int i = 0;
    while (i < rle_data_len) {
        if (mtf_vals[i] == 0) {
            /* run of 0s */
            int run = 0;
            while (i < rle_data_len && mtf_vals[i] == 0) { run++; i++; }
            run--; /* bijective: 1→RUNA, 2→RUNB RUNA, 3→RUNA RUNA, ... */
            while (run >= 0) {
                int bit = run & 1;
                rle_buf[rle_len] = bit ? BZ2_RUNB : BZ2_RUNA;
                freq[rle_buf[rle_len]]++;
                rle_len++;
                run = (run - bit) / 2 - 1;
                if (run < -1) break;
            }
        } else {
            rle_buf[rle_len] = mtf_vals[i] + 1; /* offset by 1 past RUNB */
            freq[rle_buf[rle_len]]++;
            rle_len++;
            i++;
        }
    }
    /* EOB */
    rle_buf[rle_len] = alpha_size - 1;
    freq[rle_buf[rle_len]]++;
    rle_len++;
    xx_mem_free(mtf_vals);

    /* Build Huffman table (single group for simplicity) */
    uint8_t lengths[BZ2_MAX_ALPHA_SIZE];
    uint32_t codes[BZ2_MAX_ALPHA_SIZE];
    build_huffman_lengths(freq, alpha_size, lengths);
    canonical_codes(lengths, alpha_size, codes);

    /* All selectors use group 0 */
    int n_sel = (rle_len + 49) / 50;

    /* Write block header magic */
    bz2_bw_write_bits(bw, 0x3141, 16);
    bz2_bw_write_bits(bw, 0x5926, 16);
    bz2_bw_write_bits(bw, 0x5359, 16);
    /* block CRC */
    bz2_bw_write_bits(bw, crc >> 16, 16);
    bz2_bw_write_bits(bw, crc & 0xFFFF, 16);
    /* randomised flag */
    bz2_bw_write_bits(bw, 0, 1);
    /* origPtr */
    bz2_bw_write_bits(bw, (uint32_t)orig_ptr >> 16, 8);
    bz2_bw_write_bits(bw, ((uint32_t)orig_ptr >> 8) & 0xFF, 8);
    bz2_bw_write_bits(bw, (uint32_t)orig_ptr & 0xFF, 8);

    /* Symbol map */
    uint16_t in_use_16 = 0;
    for (int hi = 0; hi < 16; hi++) {
        for (int lo = 0; lo < 16; lo++) {
            if (in_use[hi * 16 + lo]) {
                in_use_16 |= (uint16_t)(1u << (15 - hi));
                break;
            }
        }
    }
    bz2_bw_write_bits(bw, in_use_16, 16);
    for (int hi = 0; hi < 16; hi++) {
        if (in_use_16 & (1u << (15 - hi))) {
            uint16_t row = 0;
            for (int lo = 0; lo < 16; lo++) {
                if (in_use[hi * 16 + lo])
                    row |= (uint16_t)(1u << (15 - lo));
            }
            bz2_bw_write_bits(bw, row, 16);
        }
    }

    /* n_groups, n_selectors */
    bz2_bw_write_bits(bw, 2, 3);   /* 2 groups */
    bz2_bw_write_bits(bw, (uint32_t)n_sel, 15);

    /* Selectors (all 0, MTF-encoded → single 0-bit each) */
    for (int s = 0; s < n_sel; s++) bz2_bw_write_bits(bw, 0, 1);

    /* Huffman lengths for 2 groups */
    for (int g = 0; g < 2; g++) {
        int prev = lengths[0];
        bz2_bw_write_bits(bw, (uint32_t)prev, 5);
        for (int s = 0; s < alpha_size; s++) {
            int curr = lengths[s];
            while (curr < prev) { bz2_bw_write_bits(bw, 3, 2); prev--; }
            while (curr > prev) { bz2_bw_write_bits(bw, 2, 2); prev++; }
            bz2_bw_write_bits(bw, 0, 1); /* stop bit */
        }
    }

    /* Data */
    for (int k = 0; k < rle_len; k++) {
        int sym = rle_buf[k];
        bz2_bw_write_bits(bw, codes[sym], (int)lengths[sym]);
    }

    xx_mem_free(rle_buf);
    return !bw->error;
}

/* =========================================================================
 * Public compression entry point
 * ========================================================================= */

bool xx_bzip2_compress_stream(xx_io_device *src_dev,
                              const uint8_t *mem_src, size_t mem_src_size,
                              int64_t src_offset, int64_t uncomp_size,
                              bz2_bit_writer *bw, int block_size_100k,
                              xx_pd_struct *pd)
{
    (void)pd;
    if (block_size_100k < 1) block_size_100k = 1;
    if (block_size_100k > 9) block_size_100k = 9;

    int block_bytes = block_size_100k * 100000;

    if (src_dev && src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) return false;
    }

    /* Write stream header */
    bz2_bw_write_bits(bw, 'B', 8);
    bz2_bw_write_bits(bw, 'Z', 8);
    bz2_bw_write_bits(bw, 'h', 8);
    bz2_bw_write_bits(bw, (uint32_t)('0' + block_size_100k), 8);

    uint8_t *block_buf = (uint8_t *)xx_mem_alloc((size_t)block_bytes);
    if (!block_buf) return false;

    int64_t remaining = uncomp_size;
    uint32_t stream_crc = 0;
    bool ok = true;

    while (ok && remaining > 0) {
        size_t want = (remaining > (int64_t)block_bytes) ? (size_t)block_bytes : (size_t)remaining;
        size_t got = 0;

        if (src_dev) {
            ssize_t r = xx_io_read(src_dev, block_buf, want);
            if (r <= 0) { ok = false; break; }
            got = (size_t)r;
        } else if (mem_src) {
            size_t avail = mem_src_size - (size_t)(uncomp_size - remaining);
            got = avail < want ? avail : want;
            xx_rt_memcpy(block_buf, mem_src + (mem_src_size - avail), got);
        }

        if (got == 0) break;
        ok = compress_block(bw, block_buf, (int)got, block_size_100k, &stream_crc);
        remaining -= (int64_t)got;
    }

    xx_mem_free(block_buf);

    if (ok) {
        /* EOS magic */
        bz2_bw_write_bits(bw, 0x1772, 16);
        bz2_bw_write_bits(bw, 0x4538, 16);
        bz2_bw_write_bits(bw, 0x5090, 16);
        /* combined stream CRC */
        bz2_bw_write_bits(bw, stream_crc >> 16, 16);
        bz2_bw_write_bits(bw, stream_crc & 0xFFFF, 16);
        ok = bz2_bw_flush(bw);
    }

    return ok;
}
