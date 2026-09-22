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

/* Internal header for the xx_lzma implementation. Not part of the public API. */

#ifndef XX_LZMA_INTERNAL_H
#define XX_LZMA_INTERNAL_H

#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * LZMA probability model constants (Igor Pavlov LZMA SDK, public domain)
 * ========================================================================= */
#define LZMA_NUM_STATES        12
#define LZMA_NUM_POS_BITS_MAX  4
#define LZMA_NUM_LEN_TO_POS_STATES 4
#define LZMA_NUM_ALIGN_BITS    4
#define LZMA_FULL_DISTANCES    (1 << (LZMA_NUM_LEN_TO_POS_STATES / 2 + 7))
#define LZMA_NUM_LIT_CONTEXT_BITS_MAX 8
#define LZMA_MATCH_MIN_LEN     2

/* Range coder constants */
#define RC_INIT_BYTES      5
#define RC_TOP_BITS        24
#define RC_TOP_VALUE       (1u << RC_TOP_BITS)
#define RC_BIT_MODEL_TOTAL_BITS 11
#define RC_BIT_MODEL_TOTAL (1u << RC_BIT_MODEL_TOTAL_BITS)
#define RC_MOVE_BITS       5

typedef uint16_t lzma_prob;
#define PROB_INIT_VAL ((lzma_prob)(RC_BIT_MODEL_TOTAL >> 1))

/* =========================================================================
 * Range decoder state
 * ========================================================================= */
typedef struct {
    xx_io_device   *dev;
    const uint8_t  *mem;
    size_t          mem_size;
    size_t          mem_pos;
    uint8_t         ibuf[65536];
    size_t          ibuf_pos;
    size_t          ibuf_len;
    int64_t         remaining;
    uint32_t        range;
    uint32_t        code;
    bool            eof;
    bool            error;
} lzma_range_dec;

/* =========================================================================
 * LZMA property structure
 * ========================================================================= */
typedef struct {
    int      lc;       /* literal context bits  [0..8] */
    int      lp;       /* literal position bits [0..4] */
    int      pb;       /* position bits         [0..4] */
    uint32_t dict_size;
} lzma_props;

/* =========================================================================
 * LZMA decoder state (probability arrays on heap)
 * ========================================================================= */
typedef struct {
    lzma_props  props;
    lzma_prob  *lit_probs;    /* 0x300 << (lc + lp) entries */
    lzma_prob   is_match[LZMA_NUM_STATES][1 << LZMA_NUM_POS_BITS_MAX];
    lzma_prob   is_rep[LZMA_NUM_STATES];
    lzma_prob   is_rep_g0[LZMA_NUM_STATES];
    lzma_prob   is_rep_g1[LZMA_NUM_STATES];
    lzma_prob   is_rep_g2[LZMA_NUM_STATES];
    lzma_prob   is_rep0_long[LZMA_NUM_STATES][1 << LZMA_NUM_POS_BITS_MAX];
    lzma_prob   pos_slot[LZMA_NUM_LEN_TO_POS_STATES][1 << 6];
    lzma_prob   pos_decoders[LZMA_FULL_DISTANCES - LZMA_NUM_LEN_TO_POS_STATES * 4];
    lzma_prob   pos_align[1 << LZMA_NUM_ALIGN_BITS];
    lzma_prob   len_choice[2];
    lzma_prob   len_choice2[2];
    lzma_prob   len_low [2][1 << LZMA_NUM_POS_BITS_MAX][8];
    lzma_prob   len_mid [2][1 << LZMA_NUM_POS_BITS_MAX][8];
    lzma_prob   len_high[2][256];
    /* Dictionary / sliding window */
    uint8_t    *dict;
    uint64_t    dict_pos;
    uint32_t    dict_mask;   /* dict_size - 1 (power of 2) or UINT32_MAX */
    uint32_t    dict_filled;
    uint32_t    dict_limit;
    /* Decoder state */
    int         state;
    uint32_t    rep[4];
    bool        needs_init;
} lzma_decoder;

/* =========================================================================
 * LZMA2 chunk types
 * ========================================================================= */
#define LZMA2_CONTROL_EOF         0x00
#define LZMA2_CONTROL_COPY_NO_DICT  0x01
#define LZMA2_CONTROL_COPY_DICT     0x02

/* =========================================================================
 * Internal entry points
 * ========================================================================= */

/* Parse 5-byte LZMA property block */
bool lzma_parse_props(const uint8_t *props, size_t props_size, lzma_props *out);

/* Allocate and initialise decoder state */
lzma_decoder *lzma_dec_create(const lzma_props *props);
void          lzma_dec_free(lzma_decoder *dec);

/* Decompress LZMA stream */
bool xx_lzma_decompress_stream(lzma_range_dec *rd,
                               const lzma_props *props,
                               int64_t uncomp_size,
                               xx_io_device *dst_dev,
                               uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                               xx_pd_struct *pd);

/* Decompress LZMA2 stream */
bool xx_lzma2_decompress_stream(lzma_range_dec *rd,
                                uint8_t props2_byte,
                                xx_io_device *dst_dev,
                                uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd);

/* Range decoder helpers */
bool lzma_rd_init(lzma_range_dec *rd, xx_io_device *dev,
                  const uint8_t *mem, size_t mem_size, int64_t remaining);
void lzma_rd_free(lzma_range_dec *rd);

/* Compress helpers */
bool xx_lzma_compress_stream(xx_io_device *src_dev,
                             const uint8_t *mem_src, size_t mem_src_size,
                             int64_t src_offset, int64_t uncomp_size,
                             xx_io_device *dst_dev,
                             uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                             int level,
                             uint8_t *out_props, size_t *out_props_size,
                             xx_pd_struct *pd,
                             bool write_end_marker);

bool xx_lzma2_compress_stream(xx_io_device *src_dev,
                              const uint8_t *mem_src, size_t mem_src_size,
                              int64_t src_offset, int64_t uncomp_size,
                              xx_io_device *dst_dev,
                              uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                              int level, uint8_t *out_props2_byte,
                              xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_LZMA_INTERNAL_H */
