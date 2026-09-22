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

#include "xxfclib/rt/xx_rt.h"
#include "xx_deflate_internal.h"
#include <string.h>

/* ========================================================================= */
/* --- Bit Writer Implementation                                         --- */
/* ========================================================================= */

bool xx_bw_init(xx_bit_writer *bw, xx_io_device *dev, uint8_t *mem_dst, size_t mem_cap) {
    xx_rt_memset(bw, 0, sizeof(*bw));
    bw->dev = dev;
    bw->mem_dst = mem_dst;
    bw->mem_cap = mem_cap;

    if (dev) {
        bw->buffer_cap = xx_get_file_buffer_size();
        if (bw->buffer_cap == 0) {
            bw->buffer_cap = XX_DEFAULT_FILE_BUFFER_SIZE;
        }
        bw->buffer = (uint8_t *)xx_mem_alloc(bw->buffer_cap);
        if (!bw->buffer) {
            return false;
        }
    }
    return true;
}

void xx_bw_free(xx_bit_writer *bw) {
    if (bw->buffer) {
        xx_mem_free(bw->buffer);
        bw->buffer = NULL;
    }
}

static bool xx_bw_flush_buffer(xx_bit_writer *bw) {
    if (bw->error) {
        return false;
    }
    if (bw->dev && bw->buffer_pos > 0) {
        ssize_t w = xx_io_write(bw->dev, bw->buffer, bw->buffer_pos);
        if (w != (ssize_t)bw->buffer_pos) {
            bw->error = true;
            return false;
        }
        bw->buffer_pos = 0;
    }
    return true;
}

static inline bool xx_bw_put_byte(xx_bit_writer *bw, uint8_t b) {
    if (bw->dev) {
        if (bw->buffer_pos >= bw->buffer_cap) {
            if (!xx_bw_flush_buffer(bw)) {
                return false;
            }
        }
        bw->buffer[bw->buffer_pos++] = b;
    } else if (bw->mem_dst) {
        if (bw->mem_written >= bw->mem_cap) {
            bw->error = true;
            return false;
        }
        bw->mem_dst[bw->mem_written++] = b;
    }
    bw->total_written++;
    return true;
}

static bool xx_bw_write_bits(xx_bit_writer *bw, uint32_t val, int n) {
    bw->bit_buf |= ((uint64_t)(val & ((1ULL << n) - 1))) << bw->bit_count;
    bw->bit_count += n;

    while (bw->bit_count >= 8) {
        if (!xx_bw_put_byte(bw, (uint8_t)(bw->bit_buf & 0xFF))) {
            return false;
        }
        bw->bit_buf >>= 8;
        bw->bit_count -= 8;
    }
    return true;
}

static bool xx_bw_align_byte(xx_bit_writer *bw) {
    if (bw->bit_count > 0) {
        if (!xx_bw_put_byte(bw, (uint8_t)(bw->bit_buf & 0xFF))) {
            return false;
        }
        bw->bit_buf = 0;
        bw->bit_count = 0;
    }
    return true;
}

static bool xx_bw_finish(xx_bit_writer *bw) {
    if (bw->bit_count > 0) {
        if (!xx_bw_put_byte(bw, (uint8_t)(bw->bit_buf & 0xFF))) {
            return false;
        }
        bw->bit_buf = 0;
        bw->bit_count = 0;
    }
    return xx_bw_flush_buffer(bw);
}

/* ========================================================================= */
/* --- Token and Match Structures                                        --- */
/* ========================================================================= */

typedef struct {
    uint32_t length;    /* 0 if literal, >= 3 if match */
    uint32_t distance;  /* match distance if length >= 3 */
    uint8_t  literal;   /* literal byte if length == 0 */
} xx_token;

#define XX_ENC_MAX_TOKENS 65536

/* Length and Distance Encoding Helpers */
static void xx_get_length_code(uint32_t len, bool is_deflate64, uint16_t *out_sym, uint16_t *out_ebits, int *out_nbits) {
    if (is_deflate64 && len >= 259) {
        *out_sym = 285;
        *out_ebits = (uint16_t)(len - 3);
        *out_nbits = 16;
        return;
    }
    if (len == 258) {
        *out_sym = 285;
        *out_ebits = 0;
        *out_nbits = 0;
        return;
    }
    if (len <= 10) {
        *out_sym = (uint16_t)(257 + (len - 3));
        *out_ebits = 0;
        *out_nbits = 0;
        return;
    }

    static const uint16_t base_lens[] = {
        11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static const uint8_t extra_bits[] = {
        1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  4,   5,   5,   5,   5,   0
    };
    static const uint16_t syms[] = {
        265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285
    };

    int idx = 0;
    while (idx < 20 && len >= base_lens[idx + 1]) {
        idx++;
    }

    *out_sym = syms[idx];
    *out_nbits = extra_bits[idx];
    *out_ebits = (uint16_t)(len - base_lens[idx]);
}

static void xx_get_dist_code(uint32_t dist, bool is_deflate64, uint16_t *out_sym, uint16_t *out_ebits, int *out_nbits) {
    if (dist <= 1) {
        *out_sym = 0;
        *out_ebits = 0;
        *out_nbits = 0;
        return;
    }
    if (dist <= 4) {
        *out_sym = (uint16_t)(dist - 1);
        *out_ebits = 0;
        *out_nbits = 0;
        return;
    }
    if (is_deflate64 && dist > 32768) {
        if (dist <= 49152) {
            *out_sym = 30;
            *out_ebits = (uint16_t)(dist - 32769);
            *out_nbits = 14;
        } else {
            *out_sym = 31;
            *out_ebits = (uint16_t)(dist - 49153);
            *out_nbits = 14;
        }
        return;
    }

    static const uint32_t base_dists[] = {
        5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };
    static const uint8_t extra_bits[] = {
        1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,   6,   7,   7,   8,   8,   9,    9,    10,   10,   11,   11,   12,   12,    13,    13
    };

    int idx = 0;
    while (idx < 25 && dist >= base_dists[idx + 1]) {
        idx++;
    }

    *out_sym = (uint16_t)(4 + idx);
    *out_nbits = extra_bits[idx];
    *out_ebits = (uint16_t)(dist - base_dists[idx]);
}

/* Reverse bits for LSB-first output */
static inline uint32_t xx_reverse_bits(uint32_t val, int bits) {
    uint32_t res = 0;
    for (int i = 0; i < bits; ++i) {
        res |= ((val >> i) & 1) << (bits - 1 - i);
    }
    return res;
}

/* ========================================================================= */
/* --- Huffman Tree Generation                                           --- */
/* ========================================================================= */

typedef struct {
    uint32_t freq;
    uint16_t symbol;
    int16_t  left;
    int16_t  right;
} xx_huff_node;

static void xx_build_code_lengths(const uint32_t *freqs, int num_symbols, uint8_t *out_lens, int max_bits) {
    xx_rt_memset(out_lens, 0, (size_t)num_symbols);

    xx_huff_node nodes[600];
    int num_nodes = 0;

    for (int i = 0; i < num_symbols; ++i) {
        if (freqs[i] > 0) {
            nodes[num_nodes].freq = freqs[i];
            nodes[num_nodes].symbol = (uint16_t)i;
            nodes[num_nodes].left = -1;
            nodes[num_nodes].right = -1;
            num_nodes++;
        }
    }

    if (num_nodes == 0) {
        return;
    }
    if (num_nodes == 1) {
        out_lens[nodes[0].symbol] = 1;
        return;
    }

    /* Build Huffman tree by repeatedly combining two smallest nodes */
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

        nodes[min1].freq = 0; /* Mark as combined */
        nodes[min2].freq = 0;
        cur_nodes++;
        active--;
    }

    /* Compute depths */
    int root = cur_nodes - 1;
    int stack[600];
    int depths[600];
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
            int len = d > max_bits ? max_bits : d;
            if (len == 0) len = 1;
            out_lens[nodes[u].symbol] = (uint8_t)len;
        }
    }
}

static void xx_generate_canonical_codes(const uint8_t *lens, int num_symbols, uint16_t *out_codes) {
    uint16_t count[16] = {0};
    for (int i = 0; i < num_symbols; ++i) {
        if (lens[i] > 0 && lens[i] <= 15) {
            count[lens[i]]++;
        }
    }

    uint16_t next_code[16] = {0};
    uint16_t code = 0;
    for (int bits = 1; bits <= 15; ++bits) {
        code = (uint16_t)((code + count[bits - 1]) << 1);
        next_code[bits] = code;
    }

    for (int i = 0; i < num_symbols; ++i) {
        uint8_t len = lens[i];
        if (len > 0 && len <= 15) {
            uint32_t c = next_code[len]++;
            /* In Deflate, codes are written LSB first */
            out_codes[i] = (uint16_t)xx_reverse_bits(c, len);
        } else {
            out_codes[i] = 0;
        }
    }
}

/* ========================================================================= */
/* --- Block Emission (Stored, Fixed, and Dynamic)                       --- */
/* ========================================================================= */

static bool xx_emit_stored_block(xx_bit_writer *bw, const uint8_t *data, size_t len, bool bfinal) {
    if (!xx_bw_write_bits(bw, bfinal ? 1 : 0, 1)) return false;
    if (!xx_bw_write_bits(bw, 0, 2)) return false; /* BTYPE = 00 */
    if (!xx_bw_align_byte(bw)) return false;

    uint16_t length = (uint16_t)len;
    uint16_t nlength = (uint16_t)~length;
    if (!xx_bw_write_bits(bw, length, 16)) return false;
    if (!xx_bw_write_bits(bw, nlength, 16)) return false;

    for (size_t i = 0; i < len; ++i) {
        if (!xx_bw_write_bits(bw, data[i], 8)) return false;
    }
    return true;
}

static const uint8_t g_cll_order[XX_DEFLATE_MAX_CLEN_CODES] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static bool xx_emit_dynamic_block(xx_bit_writer *bw, const xx_token *tokens, size_t num_tokens,
                                  bool is_deflate64, bool bfinal) {
    uint32_t lit_freq[XX_DEFLATE_MAX_LIT_LEN_CODES] = {0};
    uint32_t dist_freq[XX_DEFLATE_MAX_DIST_CODES_64] = {0};

    lit_freq[256] = 1; /* End-of-block */

    for (size_t i = 0; i < num_tokens; ++i) {
        if (tokens[i].length == 0) {
            lit_freq[tokens[i].literal]++;
        } else {
            uint16_t sym = 0, ebits = 0;
            int nbits = 0;
            xx_get_length_code(tokens[i].length, is_deflate64, &sym, &ebits, &nbits);
            lit_freq[sym]++;

            xx_get_dist_code(tokens[i].distance, is_deflate64, &sym, &ebits, &nbits);
            dist_freq[sym]++;
        }
    }

    uint8_t lit_lens[XX_DEFLATE_MAX_LIT_LEN_CODES];
    uint8_t dist_lens[XX_DEFLATE_MAX_DIST_CODES_64];
    uint16_t lit_codes[XX_DEFLATE_MAX_LIT_LEN_CODES];
    uint16_t dist_codes[XX_DEFLATE_MAX_DIST_CODES_64];

    xx_build_code_lengths(lit_freq, XX_DEFLATE_MAX_LIT_LEN_CODES, lit_lens, 15);
    int max_dists = is_deflate64 ? XX_DEFLATE_MAX_DIST_CODES_64 : XX_DEFLATE_MAX_DIST_CODES_STD;
    xx_build_code_lengths(dist_freq, max_dists, dist_lens, 15);

    /* Determine HLIT and HDIST */
    int hlit = XX_DEFLATE_MAX_LIT_LEN_CODES;
    while (hlit > 257 && lit_lens[hlit - 1] == 0) {
        hlit--;
    }
    int hdist = max_dists;
    while (hdist > 1 && dist_lens[hdist - 1] == 0) {
        hdist--;
    }

    xx_generate_canonical_codes(lit_lens, XX_DEFLATE_MAX_LIT_LEN_CODES, lit_codes);
    xx_generate_canonical_codes(dist_lens, max_dists, dist_codes);

    /* Encode lit_lens and dist_lens with RLE */
    uint8_t combined[XX_DEFLATE_MAX_LIT_LEN_CODES + XX_DEFLATE_MAX_DIST_CODES_64];
    xx_rt_memcpy(combined, lit_lens, (size_t)hlit);
    xx_rt_memcpy(combined + hlit, dist_lens, (size_t)hdist);
    int total_lens = hlit + hdist;

    uint8_t rle_syms[600];
    uint8_t rle_extra[600];
    uint8_t rle_nbits[600];
    int num_rle = 0;
    uint32_t clen_freq[XX_DEFLATE_MAX_CLEN_CODES] = {0};

    int idx = 0;
    while (idx < total_lens) {
        uint8_t val = combined[idx];
        int run = 1;
        while (idx + run < total_lens && combined[idx + run] == val) {
            run++;
        }

        if (val == 0) {
            while (run >= 11) {
                int count = run > 138 ? 138 : run;
                rle_syms[num_rle] = 18;
                rle_extra[num_rle] = (uint8_t)(count - 11);
                rle_nbits[num_rle] = 7;
                clen_freq[18]++;
                num_rle++;
                run -= count;
                idx += count;
            }
            while (run >= 3) {
                int count = run > 10 ? 10 : run;
                rle_syms[num_rle] = 17;
                rle_extra[num_rle] = (uint8_t)(count - 3);
                rle_nbits[num_rle] = 3;
                clen_freq[17]++;
                num_rle++;
                run -= count;
                idx += count;
            }
            while (run > 0) {
                rle_syms[num_rle] = 0;
                rle_extra[num_rle] = 0;
                rle_nbits[num_rle] = 0;
                clen_freq[0]++;
                num_rle++;
                run--;
                idx++;
            }
        } else {
            rle_syms[num_rle] = val;
            rle_extra[num_rle] = 0;
            rle_nbits[num_rle] = 0;
            clen_freq[val]++;
            num_rle++;
            idx++;
            run--;

            while (run >= 3) {
                int count = run > 6 ? 6 : run;
                rle_syms[num_rle] = 16;
                rle_extra[num_rle] = (uint8_t)(count - 3);
                rle_nbits[num_rle] = 2;
                clen_freq[16]++;
                num_rle++;
                run -= count;
                idx += count;
            }
            while (run > 0) {
                rle_syms[num_rle] = val;
                rle_extra[num_rle] = 0;
                rle_nbits[num_rle] = 0;
                clen_freq[val]++;
                num_rle++;
                run--;
                idx++;
            }
        }
    }

    uint8_t clen_lens[XX_DEFLATE_MAX_CLEN_CODES];
    uint16_t clen_codes[XX_DEFLATE_MAX_CLEN_CODES];
    xx_build_code_lengths(clen_freq, XX_DEFLATE_MAX_CLEN_CODES, clen_lens, 7);
    xx_generate_canonical_codes(clen_lens, XX_DEFLATE_MAX_CLEN_CODES, clen_codes);

    int hclen = XX_DEFLATE_MAX_CLEN_CODES;
    while (hclen > 4 && clen_lens[g_cll_order[hclen - 1]] == 0) {
        hclen--;
    }

    /* Write Block Header */
    if (!xx_bw_write_bits(bw, bfinal ? 1 : 0, 1)) return false;
    if (!xx_bw_write_bits(bw, 2, 2)) return false; /* BTYPE = 10 (Dynamic) */
    if (!xx_bw_write_bits(bw, (uint32_t)(hlit - 257), 5)) return false;
    if (!xx_bw_write_bits(bw, (uint32_t)(hdist - 1), 5)) return false;
    if (!xx_bw_write_bits(bw, (uint32_t)(hclen - 4), 4)) return false;

    for (int i = 0; i < hclen; ++i) {
        if (!xx_bw_write_bits(bw, clen_lens[g_cll_order[i]], 3)) return false;
    }

    /* Write RLE-encoded lengths */
    for (int i = 0; i < num_rle; ++i) {
        uint8_t s = rle_syms[i];
        if (!xx_bw_write_bits(bw, clen_codes[s], clen_lens[s])) return false;
        if (rle_nbits[i] > 0) {
            if (!xx_bw_write_bits(bw, rle_extra[i], rle_nbits[i])) return false;
        }
    }

    /* Write Tokens */
    for (size_t i = 0; i < num_tokens; ++i) {
        if (tokens[i].length == 0) {
            uint8_t b = tokens[i].literal;
            if (!xx_bw_write_bits(bw, lit_codes[b], lit_lens[b])) return false;
        } else {
            uint16_t lsym = 0, lextra = 0;
            int lnbits = 0;
            xx_get_length_code(tokens[i].length, is_deflate64, &lsym, &lextra, &lnbits);
            if (!xx_bw_write_bits(bw, lit_codes[lsym], lit_lens[lsym])) return false;
            if (lnbits > 0) {
                if (!xx_bw_write_bits(bw, lextra, lnbits)) return false;
            }

            uint16_t dsym = 0, dextra = 0;
            int dnbits = 0;
            xx_get_dist_code(tokens[i].distance, is_deflate64, &dsym, &dextra, &dnbits);
            if (!xx_bw_write_bits(bw, dist_codes[dsym], dist_lens[dsym])) return false;
            if (dnbits > 0) {
                if (!xx_bw_write_bits(bw, dextra, dnbits)) return false;
            }
        }
    }

    /* End-of-block */
    if (!xx_bw_write_bits(bw, lit_codes[256], lit_lens[256])) return false;

    return true;
}

/* ========================================================================= */
/* --- LZ77 Match Finder & Streaming Compression Engine                 --- */
/* ========================================================================= */

#define XX_HASH_BITS 15
#define XX_HASH_SIZE (1 << XX_HASH_BITS)
#define XX_HASH_MASK (XX_HASH_SIZE - 1)

static inline uint32_t xx_calc_hash(const uint8_t *p) {
    return ((((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ (uint32_t)p[2]) & XX_HASH_MASK);
}

bool xx_deflate_compress_stream(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size,
                                int64_t src_offset, int64_t uncomp_size,
                                xx_bit_writer *writer, int level, bool is_deflate64,
                                xx_pd_struct *pd) {
    if (level < 0) level = XX_DEFLATE_LEVEL_DEFAULT;
    if (level > 9) level = XX_DEFLATE_LEVEL_BEST;

    size_t win_size = is_deflate64 ? XX_DEFLATE_WINDOW_SIZE_64K : XX_DEFLATE_WINDOW_SIZE_32K;
    size_t in_buf_cap = xx_get_file_buffer_size();
    if (in_buf_cap < win_size * 2) {
        in_buf_cap = win_size * 2;
    }

    uint8_t *in_window = (uint8_t *)xx_mem_alloc(in_buf_cap + win_size);
    if (!in_window) {
        return false;
    }

    int32_t *hash_head = (int32_t *)xx_mem_alloc(XX_HASH_SIZE * sizeof(int32_t));
    int32_t *hash_prev = (int32_t *)xx_mem_alloc((in_buf_cap + win_size) * sizeof(int32_t));
    xx_token *tokens = (xx_token *)xx_mem_alloc(XX_ENC_MAX_TOKENS * sizeof(xx_token));

    if (!hash_head || !hash_prev || !tokens) {
        if (in_window) xx_mem_free(in_window);
        if (hash_head) xx_mem_free(hash_head);
        if (hash_prev) xx_mem_free(hash_prev);
        if (tokens) xx_mem_free(tokens);
        return false;
    }

    for (int i = 0; i < XX_HASH_SIZE; ++i) {
        hash_head[i] = -1;
    }

    if (src_dev && src_offset >= 0) {
        xx_io_seek64(src_dev, src_offset, SEEK_SET);
    }

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, uncomp_size > 0 ? (uint64_t)uncomp_size : 0,
                                     is_deflate64 ? "Compressing Deflate64" : "Compressing Deflate");
    }

    size_t max_chain = (level <= 3) ? 4 : (level <= 6 ? 16 : 128);
    size_t max_match_len = is_deflate64 ? 65535 : 258;
    if (max_match_len > 258 && level <= 3) {
        max_match_len = 258;
    }

    int64_t remaining = uncomp_size;
    int64_t processed = 0;
    size_t win_head = 0; /* Current valid data start in in_window */
    size_t win_tail = 0; /* Current valid data end in in_window */
    size_t mem_pos = 0;
    bool src_eof = false;
    bool success = true;
    bool final_block_emitted = false;

    while (!src_eof || win_head < win_tail) {
        if (pd && xx_pd_is_stopped(pd)) {
            success = false;
            break;
        }

        /* Shift window back if reaching near end of allocated space */
        if (win_head > win_size && win_tail > in_buf_cap) {
            size_t shift = win_head - win_size;
            xx_rt_memmove(in_window, in_window + shift, win_tail - shift);
            for (int i = 0; i < XX_HASH_SIZE; ++i) {
                if (hash_head[i] >= 0) {
                    hash_head[i] = (hash_head[i] >= (int32_t)shift) ? (hash_head[i] - (int32_t)shift) : -1;
                }
            }
            for (size_t i = 0; i < win_tail - shift; ++i) {
                int32_t p = hash_prev[i + shift];
                hash_prev[i] = (p >= (int32_t)shift) ? (p - (int32_t)shift) : -1;
            }
            win_head -= shift;
            win_tail -= shift;
        }

        /* Read more data into window */
        while (!src_eof && (win_tail - win_head) < win_size && win_tail < in_buf_cap + win_size) {
            size_t to_read = (in_buf_cap + win_size) - win_tail;
            if (remaining >= 0 && (int64_t)to_read > remaining) {
                to_read = (size_t)remaining;
            }

            if (to_read == 0) {
                src_eof = true;
                break;
            }

            ssize_t n_read = 0;
            if (src_dev) {
                n_read = xx_io_read(src_dev, in_window + win_tail, to_read);
                if (n_read <= 0) {
                    src_eof = true;
                    break;
                }
            } else if (mem_src) {
                if (mem_pos >= mem_src_size) {
                    src_eof = true;
                    break;
                }
                size_t avail = mem_src_size - mem_pos;
                n_read = (avail < to_read) ? (ssize_t)avail : (ssize_t)to_read;
                xx_rt_memcpy(in_window + win_tail, mem_src + mem_pos, (size_t)n_read);
                mem_pos += (size_t)n_read;
                if (mem_pos >= mem_src_size) {
                    src_eof = true;
                }
            } else {
                src_eof = true;
                break;
            }

            win_tail += (size_t)n_read;
            if (remaining >= 0) {
                remaining -= n_read;
                if (remaining == 0) {
                    src_eof = true;
                }
            }
        }

        if (win_head >= win_tail) {
            break;
        }

        /* Handle Level 0 (Stored blocks directly) */
        if (level == XX_DEFLATE_LEVEL_STORED) {
            while (win_head < win_tail) {
                size_t chunk = win_tail - win_head;
                if (chunk > 65535) chunk = 65535;
                bool is_last = (src_eof && (win_head + chunk == win_tail));
                if (!xx_emit_stored_block(writer, in_window + win_head, chunk, is_last)) {
                    success = false;
                    break;
                }
                if (is_last) {
                    final_block_emitted = true;
                }
                win_head += chunk;
                processed += (int64_t)chunk;
            }
            if (!success) break;
            win_head = 0;
            win_tail = 0;
            if (final_block_emitted) {
                break;
            }
            continue;
        }

        /* LZ77 Match Finding into Tokens */
        size_t num_tokens = 0;
        size_t block_start_pos = win_head;

        while (win_head < win_tail && num_tokens < XX_ENC_MAX_TOKENS - 4) {
            size_t available = win_tail - win_head;
            if (available < 3) {
                /* Not enough bytes for match, emit literal */
                tokens[num_tokens].length = 0;
                tokens[num_tokens].literal = in_window[win_head++];
                num_tokens++;
                continue;
            }

            uint32_t h = xx_calc_hash(in_window + win_head);
            int32_t match_pos = hash_head[h];
            hash_prev[win_head] = match_pos;
            hash_head[h] = (int32_t)win_head;

            size_t best_len = 0;
            size_t best_dist = 0;
            size_t chain_count = 0;

            while (match_pos >= 0 && chain_count++ < max_chain) {
                size_t dist = win_head - (size_t)match_pos;
                if (dist > win_size) {
                    break;
                }

                if (in_window[match_pos + best_len] == in_window[win_head + best_len] &&
                    in_window[match_pos] == in_window[win_head]) {
                    size_t len = 0;
                    size_t max_test = available < max_match_len ? available : max_match_len;
                    while (len < max_test && in_window[win_head + len] == in_window[match_pos + len]) {
                        len++;
                    }
                    if (len > best_len) {
                        best_len = len;
                        best_dist = dist;
                        if (best_len >= max_match_len) {
                            break;
                        }
                    }
                }

                match_pos = hash_prev[match_pos];
            }

            if (best_len >= 3) {
                tokens[num_tokens].length = (uint32_t)best_len;
                tokens[num_tokens].distance = (uint32_t)best_dist;
                num_tokens++;

                /* Insert intermediate positions into hash table */
                for (size_t k = 1; k < best_len && (win_head + k + 2) < win_tail; ++k) {
                    uint32_t kh = xx_calc_hash(in_window + win_head + k);
                    hash_prev[win_head + k] = hash_head[kh];
                    hash_head[kh] = (int32_t)(win_head + k);
                }
                win_head += best_len;
            } else {
                tokens[num_tokens].length = 0;
                tokens[num_tokens].literal = in_window[win_head++];
                num_tokens++;
            }

            /* Block size threshold: emit block every ~32KB to 64KB input */
            if ((win_head - block_start_pos) >= 32768) {
                break;
            }
        }

        bool is_last_block = (src_eof && win_head >= win_tail);
        if (!xx_emit_dynamic_block(writer, tokens, num_tokens, is_deflate64, is_last_block)) {
            success = false;
            break;
        }
        if (is_last_block) {
            final_block_emitted = true;
            break;
        }

        processed += (int64_t)(win_head - block_start_pos);
        if (pd && pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, (uint64_t)processed);
        }
    }

    if (success && !final_block_emitted) {
        if (!xx_emit_stored_block(writer, NULL, 0, true)) {
            success = false;
        }
    }

    if (success) {
        if (!xx_bw_finish(writer)) {
            success = false;
        }
    }

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    xx_mem_free(in_window);
    xx_mem_free(hash_head);
    xx_mem_free(hash_prev);
    xx_mem_free(tokens);

    return success;
}
