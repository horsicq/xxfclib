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

#ifndef XX_BZIP2_INTERNAL_H
#define XX_BZIP2_INTERNAL_H

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Bzip2 format constants
 * ------------------------------------------------------------------------- */
#define BZ2_MAX_BLOCK_SIZE   900000   /* max uncompressed block payload */
#define BZ2_N_GROUPS         6        /* max Huffman tables per block */
#define BZ2_N_ITERS          4        /* Huffman refinement iterations */
#define BZ2_MAX_ALPHA_SIZE   258      /* 256 symbols + RUNA + RUNB */
#define BZ2_MAX_CODE_LEN     20       /* max Huffman code length */
#define BZ2_RUNA             0
#define BZ2_RUNB             1
#define BZ2_MAX_SELECTORS    32767    /* 18002 theoretically sufficient */
#define BZ2_NUM_OVERSHOOT    2
#define BZ2_BWT_RADIX_BITS   16       /* BWT suffix-sort radix window */

extern const uint32_t bz2_crc32_table[256];

/* -------------------------------------------------------------------------
 * Bit-stream reader
 * ------------------------------------------------------------------------- */
typedef struct {
    xx_io_device   *dev;
    const uint8_t  *mem;
    size_t          mem_size;
    size_t          mem_pos;
    uint8_t         ibuf[65536];
    size_t          ibuf_pos;
    size_t          ibuf_len;
    int64_t         remaining;
    uint64_t        bits;
    int             n_bits;
    bool            eof;
    bool            error;
} bz2_bit_reader;

/* -------------------------------------------------------------------------
 * Bit-stream writer
 * ------------------------------------------------------------------------- */
typedef struct {
    xx_io_device   *dev;
    uint8_t        *mem;
    size_t          mem_cap;
    size_t          mem_pos;
    uint8_t         obuf[65536];
    size_t          obuf_pos;
    int64_t         total_written;
    uint64_t        bits;
    int             n_bits;
    bool            error;
} bz2_bit_writer;

/* -------------------------------------------------------------------------
 * Internal engine entry points
 * ------------------------------------------------------------------------- */
bool xx_bzip2_decompress_stream(bz2_bit_reader *br,
                                xx_io_device *dst_dev,
                                uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd);

bool xx_bzip2_compress_stream(xx_io_device *src_dev,
                              const uint8_t *mem_src, size_t mem_src_size,
                              int64_t src_offset, int64_t uncomp_size,
                              bz2_bit_writer *bw, int block_size_100k,
                              xx_pd_struct *pd);

bool bz2_br_init(bz2_bit_reader *br, xx_io_device *dev,
                 const uint8_t *mem, size_t mem_size, int64_t remaining);
void bz2_br_free(bz2_bit_reader *br);

bool bz2_bw_init(bz2_bit_writer *bw, xx_io_device *dev, uint8_t *mem, size_t mem_cap);
bool bz2_bw_write_bits(bz2_bit_writer *bw, uint32_t val, int n);
bool bz2_bw_flush(bz2_bit_writer *bw);
void bz2_bw_free(bz2_bit_writer *bw);

#ifdef __cplusplus
}
#endif

#endif /* XX_BZIP2_INTERNAL_H */
