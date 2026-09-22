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

#ifndef XX_DEFLATE_INTERNAL_H
#define XX_DEFLATE_INTERNAL_H

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DEFLATE_WINDOW_SIZE_32K  32768
#define XX_DEFLATE_WINDOW_SIZE_64K  65536

#define XX_DEFLATE_MAX_LIT_LEN_CODES 286
#define XX_DEFLATE_MAX_DIST_CODES_STD 30
#define XX_DEFLATE_MAX_DIST_CODES_64  32
#define XX_DEFLATE_MAX_CLEN_CODES    19

#define XX_DEFLATE_MAX_BITS          15

/* Huffman decode lookup entry */
typedef struct {
    uint8_t  bits;   /* Code length in bits */
    uint16_t sym;    /* Symbol value */
} xx_huff_entry;

/* Canonical Huffman Decoder */
typedef struct {
    int num_symbols;
    xx_huff_entry fast[1 << 9]; /* 9-bit fast lookup table */
    uint16_t count[16];         /* Number of codes of each length */
    uint16_t offset[16];        /* Offset into symbol table for each length */
    uint16_t symbols[320];      /* Symbols sorted by code length */
} xx_huff_decoder;

/* Bit Reader for streaming decompression */
typedef struct {
    xx_io_device *dev;
    const uint8_t *mem_src;
    size_t       mem_size;
    size_t       mem_pos;
    uint8_t     *buffer;
    size_t       buffer_cap;
    size_t       buffer_pos;
    size_t       buffer_len;
    int64_t      remaining_input;
    uint64_t     bit_buf;
    int          bit_count;
    bool         eof;
    bool         error;
} xx_bit_reader;

/* Bit Writer for streaming compression */
typedef struct {
    xx_io_device *dev;
    uint8_t     *mem_dst;
    size_t       mem_cap;
    size_t       mem_written;
    uint8_t     *buffer;
    size_t       buffer_cap;
    size_t       buffer_pos;
    int64_t      total_written;
    uint64_t     bit_buf;
    int          bit_count;
    bool         error;
} xx_bit_writer;

/* Bit Reader and Writer helper functions */
bool xx_br_init(xx_bit_reader *br, xx_io_device *dev, const uint8_t *mem_src, size_t mem_size, int64_t remaining_input);
void xx_br_free(xx_bit_reader *br);

bool xx_bw_init(xx_bit_writer *bw, xx_io_device *dev, uint8_t *mem_dst, size_t mem_cap);
void xx_bw_free(xx_bit_writer *bw);

/* Decompressor engine internal entry point */
bool xx_deflate_decompress_stream(xx_bit_reader *reader, xx_io_device *dst_dev,
                                  uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                  bool is_deflate64, xx_pd_struct *pd);

/* Compressor engine internal entry point */
bool xx_deflate_compress_stream(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size,
                                int64_t src_offset, int64_t uncomp_size,
                                xx_bit_writer *writer, int level, bool is_deflate64,
                                xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_DEFLATE_INTERNAL_H */
