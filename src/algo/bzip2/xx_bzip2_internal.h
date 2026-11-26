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
#define BZ2_MAX_BLOCK_SIZE 900000 /* max block bytes after initial RLE */
#define BZ2_N_GROUPS 6            /* max Huffman tables per block */
#define BZ2_N_ITERS 4             /* Huffman refinement iterations */
#define BZ2_MAX_ALPHA_SIZE 258    /* 256 symbols + RUNA + RUNB */
#define BZ2_MAX_CODE_LEN 20       /* max Huffman code length */
#define BZ2_RUNA 0
#define BZ2_RUNB 1
#define BZ2_MAX_SELECTORS 32767 /* 18002 theoretically sufficient */
#define BZ2_NUM_OVERSHOOT 2
#define BZ2_BWT_RADIX_BITS 16 /* BWT suffix-sort radix window */

/* -------------------------------------------------------------------------
 * Bit-stream reader
 * ------------------------------------------------------------------------- */
typedef struct {
    xx_io_device *dev;
    const uint8_t *mem;
    size_t mem_size;
    size_t mem_pos;
    uint8_t *ibuf;
    size_t ibuf_capacity;
    size_t ibuf_pos;
    size_t ibuf_len;
    int64_t remaining;
    uint64_t bits;
    int n_bits;
    bool eof;
    bool error;
} bz2_bit_reader;

/* -------------------------------------------------------------------------
 * Bit-stream writer
 * ------------------------------------------------------------------------- */
typedef struct {
    xx_io_device *dev;
    uint8_t *mem;
    size_t mem_cap;
    size_t mem_pos;
    uint8_t *obuf;
    size_t obuf_capacity;
    size_t obuf_pos;
    int64_t total_written;
    uint64_t bits;
    int n_bits;
    bool error;
} bz2_bit_writer;

/* -------------------------------------------------------------------------
 * Internal engine entry points
 * ------------------------------------------------------------------------- */
/* Circular BWT with ascending source indices for identical rotations.
 * Buffers must not overlap. The declared block capacity is checked before
 * allocation; invalid arguments or allocation failure leave outputs intact.
 * Scratch storage is 16 * length bytes, at most 14,400,000 bytes. */
bool xx_bzip2_bwt_transform(const uint8_t *src, int length, uint8_t *bwt, int *orig_ptr);

/* Valid 1..20-bit prefix-code lengths for 1..258 symbols. Zero frequencies
 * receive weight 1. Invalid arguments leave the output unchanged. */
bool xx_bzip2_huffman_lengths(const uint32_t *freq, int symbol_count, uint8_t *lengths);

bool xx_bzip2_decompress_stream(bz2_bit_reader *br, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written, xx_pd_struct *pd);

bool xx_bzip2_compress_stream(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size, int64_t src_offset, int64_t uncomp_size, bz2_bit_writer *bw,
                              int block_size_100k, xx_pd_struct *pd);

/* -------------------------------------------------------------------------
 * Block decoder (shared by the sequential and the multi-threaded decoder)
 * ------------------------------------------------------------------------- */
/* Multi-threaded block decoding: XX_BZIP2_MT=1 builds it (such a build must
 * also compile src/threads/xx_threads.c and its platform file); the full and
 * formats profiles define it, component builds leave it out (0).  At run time
 * the environment variable XX_BZIP2_MT=0 turns it off and XX_BZIP2_MT=<n>
 * limits the number of worker threads. */
#ifndef XX_BZIP2_MT
#define XX_BZIP2_MT 0
#endif

#define BZ2_HUFF_FAST_BITS 10
#define BZ2_HUFF_FAST_SIZE (1 << BZ2_HUFF_FAST_BITS)
#define BZ2_GROUP_SIZE 50
/* Slack after the per-block arrays: vector stores and loads may run past the
 * last element. */
#define BZ2_WORK_PAD 64

typedef struct {
    uint16_t fast[BZ2_HUFF_FAST_SIZE];       /* (symbol << 5) | length for codes of at most 10 bits; 0: longer */
    uint32_t upper[BZ2_MAX_CODE_LEN + 2];    /* left-justified 20-bit end of the codes of each length */
    int32_t offset[BZ2_MAX_CODE_LEN + 1];    /* perm index of a code of this length, minus the code */
    uint16_t perm[BZ2_MAX_ALPHA_SIZE];       /* symbols ordered by code */
    int max_len;
} bz2_huffman;

/* Scratch memory of one decoder thread. */
typedef struct {
    uint32_t *tt;  /* block_cap + pad entries: (rank << 8) | byte, later LF^k */
    uint32_t *tt2; /* block_cap + pad entries */
    uint8_t *mid;  /* block_cap + pad bytes: the block before the final run-length stage */
    int block_cap;
    bool simd; /* SSSE3 move-to-front and SSE2 helpers */
    uint32_t n;
    uint32_t limit;
    uint32_t run_count;
    uint32_t run_weight;
    unsigned eob;
    uint32_t freq[256];
    uint8_t mtf[256 + 32];
    uint8_t shuf_front[16][16];
    uint8_t mask_low[16][16];
    uint8_t mask_first[2][16];
    uint8_t sel[BZ2_MAX_SELECTORS];
    bz2_huffman htabs[BZ2_N_GROUPS];
} bz2_block_work;

typedef struct {
    uint32_t n;          /* bytes before the final run-length stage, 1..block size */
    uint32_t orig_ptr;   /* < n */
    uint32_t stored_crc; /* CRC in the block header */
    bool randomised;
} bz2_block_info;

/* Resumable decoder of the final run-length stage (4 equal bytes + count). */
typedef struct {
    size_t pos;
    size_t lit_end;
    uint32_t pend;
    uint8_t pend_byte;
    bool has_run;
} bz2_rle1;

bool bz2_work_init(bz2_block_work *w, int block_bytes);
void bz2_work_free(bz2_block_work *w);
/* Reads one block (its 48-bit magic already consumed): header, Huffman,
 * move-to-front and zero-run stages. False: invalid data or input ran out. */
bool bz2_block_read(bz2_bit_reader *br, bz2_block_work *w, int block_bytes, bz2_block_info *info);
/* The same for a block whose magic starts at bit bit_pos of mem (bit 0 is the
 * top bit of mem[0]); *end_bit receives the bit after the block. */
bool bz2_block_read_mem(const uint8_t *mem, size_t size, uint64_t bit_pos, bz2_block_work *w, int block_bytes, bz2_block_info *info, uint64_t *end_bit);
/* Inverse BWT (and de-randomisation) of the block just read into w->mid. */
void bz2_block_ibwt(bz2_block_work *w, const bz2_block_info *info);
void bz2_rle1_init(bz2_rle1 *st);
/* Writes at most cap bytes; the block is finished once bz2_rle1_done. mid
 * must be readable for BZ2_WORK_PAD bytes after n. */
size_t bz2_rle1_expand(bz2_rle1 *st, const uint8_t *mid, size_t n, uint8_t *dst, size_t cap);
bool bz2_rle1_done(const bz2_rle1 *st, size_t n);
/* Runs the final run-length stage of a block (mid, n) and writes it to dev
 * in one call: *buf (*cap bytes, at least 1) grows when the block is larger.
 * False: the device refused the data. */
bool bz2_block_emit(xx_io_device *dev, const uint8_t *mid, uint32_t n, uint8_t **buf, size_t *cap, uint32_t *out_crc);
/* Positions the reader of bz2_br_init at the next bit after skipping n bits. */
bool bz2_br_skip_bits(bz2_bit_reader *br, int n);

#if XX_BZIP2_MT
/* Number of worker threads the multi-threaded decoder may use, 0: none. */
int bz2_mt_thread_count(void);
#define BZ2_MT_CONTINUE 0 /* the reader stands at the next block or end magic */
#define BZ2_MT_FAILED 1   /* the stream failed (bad CRC, write error, stop) */
/* Decodes blocks in parallel starting at the reader's position (a block
 * magic is expected there) and writes them in order to dst. Returns when
 * the next item is not a decodable block: the reader is then positioned
 * for the sequential decoder. *blocks receives the number of blocks written. */
int bz2_mt_run(bz2_bit_reader *br, xx_io_device *dst, int block_bytes, uint32_t *combined_crc, xx_pd_struct *pd, int threads, uint64_t *blocks);
#endif

bool bz2_br_init(bz2_bit_reader *br, xx_io_device *dev, const uint8_t *mem, size_t mem_size, int64_t remaining);
void bz2_br_free(bz2_bit_reader *br);

bool bz2_bw_init(bz2_bit_writer *bw, xx_io_device *dev, uint8_t *mem, size_t mem_cap);
bool bz2_bw_write_bits(bz2_bit_writer *bw, uint32_t val, int n);
bool bz2_bw_flush(bz2_bit_writer *bw);
void bz2_bw_free(bz2_bit_writer *bw);

#ifdef __cplusplus
}
#endif

#endif /* XX_BZIP2_INTERNAL_H */
