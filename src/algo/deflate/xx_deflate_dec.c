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
/* --- Bit Reader Helpers                                                --- */
/* ========================================================================= */

bool xx_br_init(xx_bit_reader *br, xx_io_device *dev,
                    const uint8_t *mem_src, size_t mem_size, int64_t remaining_input) {
    xx_rt_memset(br, 0, sizeof(*br));
    br->dev = dev;
    br->mem_src = mem_src;
    br->mem_size = mem_size;
    br->remaining_input = remaining_input;

    if (dev) {
        br->buffer_cap = xx_get_file_buffer_size();
        if (br->buffer_cap == 0) {
            br->buffer_cap = XX_DEFAULT_FILE_BUFFER_SIZE;
        }
        br->buffer = (uint8_t *)xx_mem_alloc(br->buffer_cap);
        if (!br->buffer) {
            return false;
        }
    }
    return true;
}

void xx_br_free(xx_bit_reader *br) {
    if (br->buffer) {
        xx_mem_free(br->buffer);
        br->buffer = NULL;
    }
}

static bool xx_br_refill(xx_bit_reader *br, int need_bits) {
    while (br->bit_count < need_bits) {
        uint8_t byte = 0;
        if (br->dev) {
            if (br->buffer_pos >= br->buffer_len) {
                if (br->eof) {
                    return false;
                }
                size_t to_read = br->buffer_cap;
                if (br->remaining_input >= 0) {
                    if (br->remaining_input == 0) {
                        br->eof = true;
                        return false;
                    }
                    if ((int64_t)to_read > br->remaining_input) {
                        to_read = (size_t)br->remaining_input;
                    }
                }
                ssize_t n = xx_io_read(br->dev, br->buffer, to_read);
                if (n <= 0) {
                    br->eof = true;
                    return false;
                }
                br->buffer_pos = 0;
                br->buffer_len = (size_t)n;
                if (br->remaining_input >= 0) {
                    br->remaining_input -= n;
                }
            }
            byte = br->buffer[br->buffer_pos++];
        } else if (br->mem_src) {
            if (br->mem_pos >= br->mem_size) {
                br->eof = true;
                return false;
            }
            byte = br->mem_src[br->mem_pos++];
        } else {
            return false;
        }

        br->bit_buf |= ((uint64_t)byte) << br->bit_count;
        br->bit_count += 8;
    }
    return true;
}

static inline uint32_t xx_br_peek(xx_bit_reader *br, int n) {
    if (br->bit_count < n) {
        xx_br_refill(br, n);
    }
    return (uint32_t)(br->bit_buf & ((1ULL << n) - 1));
}

static inline void xx_br_drop(xx_bit_reader *br, int n) {
    br->bit_buf >>= n;
    br->bit_count -= n;
    if (br->bit_count < 0) {
        br->bit_count = 0;
    }
}

static inline uint32_t xx_br_read(xx_bit_reader *br, int n) {
    uint32_t v = xx_br_peek(br, n);
    xx_br_drop(br, n);
    return v;
}

static inline void xx_br_align_byte(xx_bit_reader *br) {
    int drop = br->bit_count % 8;
    if (drop > 0) {
        xx_br_drop(br, drop);
    }
}

/* ========================================================================= */
/* --- Canonical Huffman Decoder Construction & Decoding                  --- */
/* ========================================================================= */

static bool xx_huff_build(xx_huff_decoder *dec, const uint8_t *lengths, int num_symbols) {
    xx_rt_memset(dec, 0, sizeof(*dec));
    dec->num_symbols = num_symbols;

    for (int i = 0; i < num_symbols; ++i) {
        uint8_t len = lengths[i];
        if (len > XX_DEFLATE_MAX_BITS) {
            return false;
        }
        if (len > 0) {
            dec->count[len]++;
        }
    }

    /* Check validity: total code space <= 1 */
    uint32_t code_space = 0;
    for (int len = 1; len <= XX_DEFLATE_MAX_BITS; ++len) {
        code_space = (code_space << 1) + dec->count[len];
    }
    if (code_space > (1U << XX_DEFLATE_MAX_BITS)) {
        return false;
    }

    /* Compute symbol offsets per length */
    uint16_t next_offset = 0;
    for (int len = 1; len <= XX_DEFLATE_MAX_BITS; ++len) {
        dec->offset[len] = next_offset;
        next_offset += dec->count[len];
    }

    uint16_t temp_offset[16];
    xx_rt_memcpy(temp_offset, dec->offset, sizeof(temp_offset));

    for (int sym = 0; sym < num_symbols; ++sym) {
        uint8_t len = lengths[sym];
        if (len > 0) {
            dec->symbols[temp_offset[len]++] = (uint16_t)sym;
        }
    }

    /* Generate canonical codes and build 9-bit fast table */
    uint32_t next_code[17];
    uint32_t cur_code = 0;
    next_code[0] = 0;
    for (int len = 1; len <= XX_DEFLATE_MAX_BITS; ++len) {
        cur_code = (cur_code + dec->count[len - 1]) << 1;
        next_code[len] = cur_code;
    }

    for (int sym = 0; sym < num_symbols; ++sym) {
        uint8_t len = lengths[sym];
        if (len == 0) continue;

        uint32_t c = next_code[len]++;

        /* Reverse code bits for LSB-first bitstream */
        uint32_t rev = 0;
        for (int b = 0; b < len; ++b) {
            rev |= ((c >> (len - 1 - b)) & 1) << b;
        }

        if (len <= 9) {
            int step = 1 << len;
            int count = 1 << (9 - len);
            for (int k = 0; k < count; ++k) {
                int idx = rev | (k * step);
                dec->fast[idx].bits = len;
                dec->fast[idx].sym = (uint16_t)sym;
            }
        }
    }

    return true;
}

static uint32_t xx_huff_reverse(uint32_t code, unsigned length) {
    uint32_t reversed = 0U;
    unsigned bit;
    for (bit = 0U; bit < length; ++bit) {
        reversed = (reversed << 1U) | (code & 1U);
        code >>= 1U;
    }
    return reversed;
}

static inline int xx_huff_decode(const xx_huff_decoder *dec, xx_bit_reader *br) {
    uint32_t code = 0U;
    uint32_t first_code = 0U;
    int len;
    /* The table is fast for ordinary data.  Do not request nine bits at the
     * end of a member, though: a valid final Huffman code can be shorter. */
    if (br->bit_count >= 9) {
        uint32_t peek9 = xx_br_peek(br, 9);
        xx_huff_entry entry = dec->fast[peek9];
        if (entry.bits > 0) {
            xx_br_drop(br, entry.bits);
            return entry.sym;
        }
    }

    /* Canonical Deflate codes are sent least-significant bit first.  Compare
     * the accumulated bit order with the reversed canonical codes directly;
     * this also handles codes longer than the nine-bit fast table. */
    for (len = 1; len <= XX_DEFLATE_MAX_BITS; ++len) {
        int index;
        if (!xx_br_refill(br, 1)) return -1;
        code |= xx_br_read(br, 1) << (len - 1);
        first_code = (first_code + dec->count[len - 1]) << 1U;
        for (index = 0; index < dec->count[len]; ++index) {
            if (code == xx_huff_reverse(first_code + (uint32_t)index,
                                        (unsigned)len)) {
                return dec->symbols[dec->offset[len] + index];
            }
        }
    }
    return -1;
}

/* ========================================================================= */
/* --- Fixed Huffman Tables Construction                                 --- */
/* ========================================================================= */

static void xx_huff_build_fixed(xx_huff_decoder *lit_dec, xx_huff_decoder *dist_dec) {
    uint8_t lit_lens[288];
    for (int i = 0; i <= 143; ++i)  lit_lens[i] = 8;
    for (int i = 144; i <= 255; ++i) lit_lens[i] = 9;
    for (int i = 256; i <= 279; ++i) lit_lens[i] = 7;
    for (int i = 280; i <= 287; ++i) lit_lens[i] = 8;
    xx_huff_build(lit_dec, lit_lens, 288);

    uint8_t dist_lens[32];
    for (int i = 0; i < 32; ++i) {
        dist_lens[i] = 5;
    }
    xx_huff_build(dist_dec, dist_lens, 32);
}

/* ========================================================================= */
/* --- Output Buffer Accumulator                                         --- */
/* ========================================================================= */

typedef struct {
    xx_io_device *dev;
    uint8_t     *mem_dst;
    size_t       mem_cap;
    size_t       mem_written;
    uint8_t     *buf;
    size_t       buf_cap;
    size_t       buf_pos;
    int64_t      total_written;
    bool         error;
} xx_out_acc;

static bool xx_out_init(xx_out_acc *out, xx_io_device *dev, uint8_t *mem_dst, size_t mem_cap) {
    xx_rt_memset(out, 0, sizeof(*out));
    out->dev = dev;
    out->mem_dst = mem_dst;
    out->mem_cap = mem_cap;

    if (dev) {
        out->buf_cap = xx_get_file_buffer_size();
        if (out->buf_cap == 0) {
            out->buf_cap = XX_DEFAULT_FILE_BUFFER_SIZE;
        }
        out->buf = (uint8_t *)xx_mem_alloc(out->buf_cap);
        if (!out->buf) {
            return false;
        }
    }
    return true;
}

static void xx_out_free(xx_out_acc *out) {
    if (out->buf) {
        xx_mem_free(out->buf);
        out->buf = NULL;
    }
}

static bool xx_out_flush(xx_out_acc *out) {
    if (out->error) {
        return false;
    }
    if (out->dev && out->buf_pos > 0) {
        ssize_t w = xx_io_write(out->dev, out->buf, out->buf_pos);
        if (w != (ssize_t)out->buf_pos) {
            out->error = true;
            return false;
        }
        out->buf_pos = 0;
    }
    return true;
}

static inline bool xx_out_put_byte(xx_out_acc *out, uint8_t b) {
    if (out->dev) {
        if (out->buf_pos >= out->buf_cap) {
            if (!xx_out_flush(out)) {
                return false;
            }
        }
        out->buf[out->buf_pos++] = b;
    } else if (out->mem_dst) {
        if (out->mem_written >= out->mem_cap) {
            out->error = true;
            return false;
        }
        out->mem_dst[out->mem_written++] = b;
    }
    out->total_written++;
    return true;
}

/* ========================================================================= */
/* --- Decompression Engine Core                                         --- */
/* ========================================================================= */

static const uint8_t g_cll_order[XX_DEFLATE_MAX_CLEN_CODES] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

bool xx_deflate_decompress_stream(xx_bit_reader *reader, xx_io_device *dst_dev,
                                  uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                  bool is_deflate64, xx_pd_struct *pd) {
    xx_out_acc out;
    if (!xx_out_init(&out, dst_dev, mem_dst, mem_cap)) {
        return false;
    }

    size_t win_size = is_deflate64 ? XX_DEFLATE_WINDOW_SIZE_64K : XX_DEFLATE_WINDOW_SIZE_32K;
    uint8_t *window = (uint8_t *)xx_mem_alloc(win_size);
    if (!window) {
        xx_out_free(&out);
        return false;
    }
    size_t win_pos = 0;

    xx_huff_decoder fixed_lit, fixed_dist;
    bool fixed_built = false;

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, 0, is_deflate64 ? "Unpacking Deflate64" : "Unpacking Deflate");
    }

    bool success = true;
    bool bfinal = false;

    while (!bfinal && !reader->error) {
        if (pd && xx_pd_is_stopped(pd)) {
            success = false;
            break;
        }

        uint32_t final_bit = xx_br_read(reader, 1);
        bfinal = (final_bit != 0);
        uint32_t btype = xx_br_read(reader, 2);

        if (btype == 0) {
            /* Block Type 0: Stored / Uncompressed */
            xx_br_align_byte(reader);
            uint16_t len = (uint16_t)xx_br_read(reader, 16);
            uint16_t nlen = (uint16_t)xx_br_read(reader, 16);
            if (len != (uint16_t)(~nlen)) {
                success = false;
                break;
            }

            for (uint32_t i = 0; i < len; ++i) {
                uint8_t b = (uint8_t)xx_br_read(reader, 8);
                window[win_pos++ & (win_size - 1)] = b;
                if (!xx_out_put_byte(&out, b)) {
                    success = false;
                    break;
                }
            }
            if (!success) break;

        } else if (btype == 1 || btype == 2) {
            /* Block Type 1 (Fixed) or Type 2 (Dynamic) */
            const xx_huff_decoder *cur_lit_dec = NULL;
            const xx_huff_decoder *cur_dist_dec = NULL;
            xx_huff_decoder dyn_lit, dyn_dist;

            if (btype == 1) {
                if (!fixed_built) {
                    xx_huff_build_fixed(&fixed_lit, &fixed_dist);
                    fixed_built = true;
                }
                cur_lit_dec = &fixed_lit;
                cur_dist_dec = &fixed_dist;
            } else {
                /* Dynamic Huffman Header */
                uint32_t hlit  = xx_br_read(reader, 5) + 257;
                uint32_t hdist = xx_br_read(reader, 5) + 1;
                uint32_t hclen = xx_br_read(reader, 4) + 4;

                uint32_t max_dist_codes = is_deflate64 ? XX_DEFLATE_MAX_DIST_CODES_64 : XX_DEFLATE_MAX_DIST_CODES_STD;
                if (hlit > XX_DEFLATE_MAX_LIT_LEN_CODES || hdist > max_dist_codes) {
                    success = false;
                    break;
                }

                uint8_t clen_lens[XX_DEFLATE_MAX_CLEN_CODES];
                xx_rt_memset(clen_lens, 0, sizeof(clen_lens));
                for (uint32_t i = 0; i < hclen; ++i) {
                    clen_lens[g_cll_order[i]] = (uint8_t)xx_br_read(reader, 3);
                }

                xx_huff_decoder clen_dec;
                if (!xx_huff_build(&clen_dec, clen_lens, XX_DEFLATE_MAX_CLEN_CODES)) {
                    success = false;
                    break;
                }

                uint32_t total_codes = hlit + hdist;
                uint8_t lit_dist_lens[XX_DEFLATE_MAX_LIT_LEN_CODES + XX_DEFLATE_MAX_DIST_CODES_64];
                uint32_t code_idx = 0;
                uint8_t prev_len = 0;

                while (code_idx < total_codes) {
                    int sym = xx_huff_decode(&clen_dec, reader);
                    if (sym < 0) {
                        success = false;
                        break;
                    }

                    if (sym <= 15) {
                        prev_len = (uint8_t)sym;
                        lit_dist_lens[code_idx++] = prev_len;
                    } else if (sym == 16) {
                        if (code_idx == 0) {
                            success = false;
                            break;
                        }
                        uint32_t repeat = xx_br_read(reader, 2) + 3;
                        if (code_idx + repeat > total_codes) {
                            success = false;
                            break;
                        }
                        while (repeat--) {
                            lit_dist_lens[code_idx++] = prev_len;
                        }
                    } else if (sym == 17) {
                        uint32_t repeat = xx_br_read(reader, 3) + 3;
                        if (code_idx + repeat > total_codes) {
                            success = false;
                            break;
                        }
                        prev_len = 0;
                        while (repeat--) {
                            lit_dist_lens[code_idx++] = 0;
                        }
                    } else if (sym == 18) {
                        uint32_t repeat = xx_br_read(reader, 7) + 11;
                        if (code_idx + repeat > total_codes) {
                            success = false;
                            break;
                        }
                        prev_len = 0;
                        while (repeat--) {
                            lit_dist_lens[code_idx++] = 0;
                        }
                    } else {
                        success = false;
                        break;
                    }
                }
                if (!success) break;

                if (!xx_huff_build(&dyn_lit, lit_dist_lens, (int)hlit)) {
                    success = false;
                    break;
                }
                if (!xx_huff_build(&dyn_dist, lit_dist_lens + hlit, (int)hdist)) {
                    success = false;
                    break;
                }

                cur_lit_dec = &dyn_lit;
                cur_dist_dec = &dyn_dist;
            }

            /* Decompress block data */
            while (true) {
                int sym = xx_huff_decode(cur_lit_dec, reader);
                if (sym < 0) {
                    success = false;
                    break;
                }

                if (sym < 256) {
                    /* Literal byte */
                    uint8_t b = (uint8_t)sym;
                    window[win_pos++ & (win_size - 1)] = b;
                    if (!xx_out_put_byte(&out, b)) {
                        success = false;
                        break;
                    }
                } else if (sym == 256) {
                    /* End of block */
                    break;
                } else if (sym <= 285) {
                    /* Length code */
                    uint32_t length = 0;
                    if (sym <= 260) {
                        length = (uint32_t)(sym - 257) + 3;
                    } else if (sym <= 284) {
                        uint32_t more_bits = (uint32_t)(sym - 261) / 4;
                        uint32_t base = ((4 + (uint32_t)(sym + 3) % 4) << more_bits) + 3;
                        length = base + xx_br_read(reader, (int)more_bits);
                    } else if (sym == 285) {
                        if (is_deflate64) {
                            length = 3 + xx_br_read(reader, 16);
                        } else {
                            length = 258;
                        }
                    }

                    /* Distance code */
                    int dist_sym = xx_huff_decode(cur_dist_dec, reader);
                    if (dist_sym < 0) {
                        success = false;
                        break;
                    }

                    uint32_t dist = 0;
                    if (dist_sym <= 3) {
                        dist = (uint32_t)dist_sym + 1;
                    } else if (dist_sym <= 29) {
                        uint32_t more_bits = ((uint32_t)dist_sym / 2) - 1;
                        uint32_t base = (2 + ((uint32_t)dist_sym % 2)) << more_bits;
                        dist = base + 1 + xx_br_read(reader, (int)more_bits);
                    } else if (is_deflate64 && (dist_sym == 30 || dist_sym == 31)) {
                        uint32_t base = (dist_sym == 30) ? 32769 : 49153;
                        dist = base + xx_br_read(reader, 14);
                    } else {
                        success = false;
                        break;
                    }

                    if (dist > win_size || dist > win_pos) {
                        success = false;
                        break;
                    }

                    /* Copy match from sliding window */
                    for (uint32_t k = 0; k < length; ++k) {
                        uint8_t b = window[(win_pos - dist) & (win_size - 1)];
                        window[win_pos++ & (win_size - 1)] = b;
                        if (!xx_out_put_byte(&out, b)) {
                            success = false;
                            break;
                        }
                    }
                    if (!success) break;
                } else {
                    success = false;
                    break;
                }
            }
            if (!success) break;

        } else {
            /* Invalid / reserved block type 3 */
            success = false;
            break;
        }

        if (pd && pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, (uint64_t)out.total_written);
        }
    }

    if (success) {
        if (!xx_out_flush(&out)) {
            success = false;
        }
    }

    if (out_written) {
        *out_written = (size_t)out.total_written;
    }

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    xx_mem_free(window);
    xx_out_free(&out);
    return success;
}
