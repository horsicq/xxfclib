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
 * Algorithm: BWT + MTF + RLE + bounded canonical Huffman
 * following Julian Seward's bzip2 format.
 *
 * Block-size multiplier: 1..9 (100 kB..900 kB).
 * Each block is independently compressed and carries its own CRC32.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_bzip2_internal.h"
#include "xx_bzip2_mtf.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* =========================================================================
 * Canonical codes for the bounded Huffman lengths. The stream carries two
 * identical tables and all selectors choose table zero.
 * ========================================================================= */

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
        else codes[i] = 0;
    }
}

/* =========================================================================
 * Compress one block
 * ========================================================================= */

/* The BZh multiplier limits the RLE output passed to BWT, not raw input.
 * Four identical input bytes occupy five RLE bytes, so even a full raw block
 * can exceed its declared capacity. Keep whole runs where they fit; the
 * unconsumed raw suffix is carried into the next block. */
static int rle_block_prefix(const uint8_t *data, int data_len, int block_bytes)
{
    int raw_len = 0;
    int rle_len = 0;
    while (raw_len < data_len) {
        int run = 1;
        while (raw_len + run < data_len && data[raw_len + run] == data[raw_len] && run < 255 + 4) {
            run++;
        }
        int encoded_len = run >= 4 ? 5 : run;
        if (encoded_len > block_bytes - rle_len) break;
        rle_len += encoded_len;
        raw_len += run;
    }
    return raw_len;
}

static bool compress_block(bz2_bit_writer *bw, const uint8_t *data, int data_len, int block_size_100k, uint32_t *stream_crc)
{
    /* Bzip2 uses the unreflected CRC-32/BZIP2 model for each block. */
    uint32_t crc = xx_crc32(XX_CRC_TYPE_CRC32_BZIP2, data, (size_t)data_len);
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
    if (rle_data_len > block_size_100k * 100000) {
        xx_mem_free(rle_data);
        return false;
    }

    /* BWT on RLE-encoded block */
    uint8_t *bwt = (uint8_t *)xx_mem_alloc((size_t)rle_data_len);
    if (!bwt) {
        xx_mem_free(rle_data);
        return false;
    }
    int orig_ptr = 0;
    if (!xx_bzip2_bwt_transform(rle_data, rle_data_len, bwt, &orig_ptr)) {
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
    for (int j = 0; j < 256; j++)
        if (in_use[j]) n_syms++;
    int alpha_size = n_syms + 2; /* +RUNA +RUNB +EOB */

    /* MTF */
    int *mtf_vals = (int *)xx_mem_alloc((size_t)rle_data_len * sizeof(int));
    if (!mtf_vals) {
        xx_mem_free(bwt);
        return false;
    }
    if (!xx_bzip2_mtf_encode(bwt, (size_t)rle_data_len, mtf_vals, in_use)) {
        xx_mem_free(mtf_vals);
        xx_mem_free(bwt);
        return false;
    }
    xx_mem_free(bwt);

    /* RLE (RUNA/RUNB) + build frequency table */
    uint32_t freq[BZ2_MAX_ALPHA_SIZE];
    xx_rt_memset(freq, 0, sizeof(freq));
    int *rle_buf = (int *)xx_mem_alloc(((size_t)rle_data_len + 2) * sizeof(int));
    if (!rle_buf) {
        xx_mem_free(mtf_vals);
        return false;
    }
    int rle_len = 0;
    int i = 0;
    while (i < rle_data_len) {
        if (mtf_vals[i] == 0) {
            /* run of 0s */
            int run = 0;
            while (i < rle_data_len && mtf_vals[i] == 0) {
                run++;
                i++;
            }
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

    /* Build a bounded Huffman table; the format carries two identical groups. */
    uint8_t lengths[BZ2_MAX_ALPHA_SIZE];
    uint32_t codes[BZ2_MAX_ALPHA_SIZE];
    if (!xx_bzip2_huffman_lengths(freq, alpha_size, lengths)) {
        xx_mem_free(rle_buf);
        return false;
    }
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
                if (in_use[hi * 16 + lo]) row |= (uint16_t)(1u << (15 - lo));
            }
            bz2_bw_write_bits(bw, row, 16);
        }
    }

    /* n_groups, n_selectors */
    bz2_bw_write_bits(bw, 2, 3); /* 2 groups */
    bz2_bw_write_bits(bw, (uint32_t)n_sel, 15);

    /* Selectors (all 0, MTF-encoded → single 0-bit each) */
    for (int s = 0; s < n_sel; s++) bz2_bw_write_bits(bw, 0, 1);

    /* Huffman lengths for 2 groups */
    for (int g = 0; g < 2; g++) {
        int prev = lengths[0];
        bz2_bw_write_bits(bw, (uint32_t)prev, 5);
        for (int s = 0; s < alpha_size; s++) {
            int curr = lengths[s];
            while (curr < prev) {
                bz2_bw_write_bits(bw, 3, 2);
                prev--;
            }
            while (curr > prev) {
                bz2_bw_write_bits(bw, 2, 2);
                prev++;
            }
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

bool xx_bzip2_compress_stream(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size, int64_t src_offset, int64_t uncomp_size, bz2_bit_writer *bw,
                              int block_size_100k, xx_pd_struct *pd)
{
    (void)pd;
    if (!bw || bw->error) return false;
    if (uncomp_size < 0 || (!src_dev && uncomp_size > 0 && (!mem_src || (uint64_t)uncomp_size > mem_src_size))) return false;
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
    size_t buffered = 0;
    uint32_t stream_crc = 0;
    bool ok = true;

    while (ok && remaining > 0) {
        size_t want = (remaining > (int64_t)block_bytes) ? (size_t)block_bytes : (size_t)remaining;
        size_t got = buffered;

        if (src_dev) {
            /* BWT blocks remain complete protocol objects; refill them through
             * the operation's bounded I/O staging size. */
            while (got < want) {
                size_t request = want - got;
                ssize_t r;
                if (request > bw->obuf_capacity) request = bw->obuf_capacity;
                r = xx_io_read(src_dev, block_buf + got, request);
                if (r <= 0 || (size_t)r > request) {
                    ok = false;
                    break;
                }
                got += (size_t)r;
            }
            if (!ok) break;
        } else if (mem_src) {
            size_t source_pos = (size_t)(uncomp_size - remaining) + got;
            xx_rt_memcpy(block_buf + got, mem_src + source_pos, want - got);
            got = want;
        }

        int block_len = rle_block_prefix(block_buf, (int)got, block_bytes);
        if (block_len == 0) {
            ok = false;
            break;
        }
        ok = compress_block(bw, block_buf, block_len, block_size_100k, &stream_crc);
        remaining -= block_len;
        buffered = got - (size_t)block_len;
        if (buffered > 0) {
            xx_rt_memmove(block_buf, block_buf + block_len, buffered);
        }
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
