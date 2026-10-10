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
#include "platforms/xx_deflate_dec_platform.h"
#include <string.h>

/* ========================================================================= */
/* --- Bit Reader Helpers                                                --- */
/* ========================================================================= */

bool xx_br_init(xx_bit_reader *br, xx_io_device *dev, const uint8_t *mem_src, size_t mem_size, int64_t remaining_input)
{
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

void xx_br_free(xx_bit_reader *br)
{
    if (br->buffer) {
        xx_mem_free(br->buffer);
        br->buffer = NULL;
    }
}

static bool xx_br_refill(xx_bit_reader *br, int need_bits)
{
    while (br->bit_count < need_bits) {
        const uint8_t *source;
        size_t available, consumed;
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
            source = br->buffer + br->buffer_pos;
            available = br->buffer_len - br->buffer_pos;
        } else if (br->mem_src) {
            if (br->mem_pos >= br->mem_size) {
                br->eof = true;
                return false;
            }
            source = br->mem_src + br->mem_pos;
            available = br->mem_size - br->mem_pos;
        } else {
            return false;
        }

        /* All callers need at most sixteen bits. Opportunistic word refill
         * removes per-byte source checks without reading beyond the supplied
         * span or overflowing the bit accumulator. Prefetched bytes remain
         * accounted for by bit_count, including following stored blocks. */
        if (available >= 4U && br->bit_count <= 32) {
            uint32_t word;
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
            memcpy(&word, source, sizeof(word));
#else
            word = (uint32_t)source[0] | ((uint32_t)source[1] << 8U) | ((uint32_t)source[2] << 16U) | ((uint32_t)source[3] << 24U);
#endif
            br->bit_buf |= (uint64_t)word << br->bit_count;
            br->bit_count += 32;
            consumed = 4U;
        } else {
            br->bit_buf |= (uint64_t)*source << br->bit_count;
            br->bit_count += 8;
            consumed = 1U;
        }
        if (br->dev) br->buffer_pos += consumed;
        else br->mem_pos += consumed;
    }
    return true;
}

static inline uint32_t xx_br_peek(xx_bit_reader *br, int n)
{
    if (br->bit_count < n) {
        xx_br_refill(br, n);
    }
    return (uint32_t)(br->bit_buf & ((1ULL << n) - 1));
}

static inline void xx_br_drop(xx_bit_reader *br, int n)
{
    br->bit_buf >>= n;
    br->bit_count -= n;
    if (br->bit_count < 0) {
        br->bit_count = 0;
    }
}

static inline uint32_t xx_br_read(xx_bit_reader *br, int n)
{
    if (br->bit_count < n && !xx_br_refill(br, n)) {
        br->error = true;
        return 0U;
    }
    uint32_t v = xx_br_peek(br, n);
    xx_br_drop(br, n);
    return v;
}

static inline void xx_br_align_byte(xx_bit_reader *br)
{
    int drop = br->bit_count % 8;
    if (drop > 0) {
        xx_br_drop(br, drop);
    }
}

/* Stored payloads are byte-aligned. Preserve any prefetched bytes before
 * copying directly from the bounded input buffer. */
static bool xx_br_read_bytes(xx_bit_reader *br, uint8_t *dst, size_t size)
{
    if (br->error || (br->bit_count & 7) != 0) return false;
    while (size != 0U) {
        size_t available, chunk;
        const uint8_t *source;
        if (br->bit_count >= 8) {
            *dst++ = (uint8_t)br->bit_buf;
            xx_br_drop(br, 8);
            --size;
            continue;
        }
        if (br->dev) {
            if (br->buffer_pos == br->buffer_len) {
                if (!xx_br_refill(br, 8)) break;
                continue;
            }
            source = br->buffer + br->buffer_pos;
            available = br->buffer_len - br->buffer_pos;
        } else if (br->mem_src) {
            source = br->mem_src + br->mem_pos;
            available = br->mem_size - br->mem_pos;
            if (available == 0U) break;
        } else {
            break;
        }
        chunk = size < available ? size : available;
        xx_rt_memcpy(dst, source, chunk);
        if (br->dev) br->buffer_pos += chunk;
        else br->mem_pos += chunk;
        dst += chunk;
        size -= chunk;
    }
    if (size != 0U) {
        br->eof = true;
        br->error = true;
        return false;
    }
    return true;
}

/* ========================================================================= */
/* --- Canonical Huffman Decoder Construction & Decoding                  --- */
/* ========================================================================= */

static bool xx_huff_build(xx_huff_decoder *dec, const uint8_t *lengths, int num_symbols)
{
    xx_rt_memset(dec, 0, sizeof(*dec));
    dec->num_symbols = num_symbols;
    dec->min_bits = XX_DEFLATE_MAX_BITS + 1;

    for (int i = 0; i < num_symbols; ++i) {
        uint8_t len = lengths[i];
        if (len > XX_DEFLATE_MAX_BITS) {
            return false;
        }
        if (len > 0) {
            dec->count[len]++;
            if (len < dec->min_bits) dec->min_bits = len;
            if (len > dec->max_bits) dec->max_bits = len;
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
        dec->first_code[len] = (uint16_t)cur_code;
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

static inline int xx_huff_decode(const xx_huff_decoder *dec, xx_bit_reader *br)
{
    uint32_t reversed;
    int len, available;
    /* Refill opportunistically. A final code can need fewer than nine bits;
     * a failed lookahead must not reject it or consume padded missing bits. */
    if (br->bit_count < 9) (void)xx_br_refill(br, 9);
    if (br->bit_count > 0) {
        xx_huff_entry entry = dec->fast[(uint32_t)br->bit_buf & 511U];
        if (entry.bits > 0 && entry.bits <= br->bit_count) {
            xx_br_drop(br, entry.bits);
            return entry.sym;
        }
    }

    /* Reverse the available prefix once. Checking canonical ranges avoids
     * the old refill/read/drop for every individual long-code bit. Missing
     * input bits never contribute to a successful final short code. */
    if (br->bit_count < dec->max_bits) (void)xx_br_refill(br, dec->max_bits);
    available = br->bit_count < dec->max_bits ? br->bit_count : dec->max_bits;
    reversed = (uint32_t)br->bit_buf & 65535U;
    reversed = ((reversed & 0x5555U) << 1U) | ((reversed >> 1U) & 0x5555U);
    reversed = ((reversed & 0x3333U) << 2U) | ((reversed >> 2U) & 0x3333U);
    reversed = ((reversed & 0x0f0fU) << 4U) | ((reversed >> 4U) & 0x0f0fU);
    reversed = ((reversed & 255U) << 8U) | (reversed >> 8U);
    len = br->bit_count >= 9 ? 10 : dec->min_bits;
    for (; len <= available; ++len) {
        uint32_t code = reversed >> (16 - len);
        uint32_t first_code = dec->first_code[len];
        if (code >= first_code && code - first_code < dec->count[len]) {
            xx_br_drop(br, len);
            return dec->symbols[dec->offset[len] + code - first_code];
        }
    }
    br->error = true;
    return -1;
}

/* ========================================================================= */
/* --- Fixed Huffman Tables Construction                                 --- */
/* ========================================================================= */

static void xx_huff_build_fixed(xx_huff_decoder *lit_dec, xx_huff_decoder *dist_dec)
{
    uint8_t lit_lens[288];
    for (int i = 0; i <= 143; ++i) lit_lens[i] = 8;
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

typedef void (*xx_decode_copy_fn)(uint8_t *, const uint8_t *, size_t);
typedef void (*xx_decode_match_fn)(uint8_t *, size_t, size_t, size_t, uint32_t, size_t);

/* Constant-size memcpy loads/stores are unaligned and alias-safe. No helper
 * reads or writes beyond the actual span, including the final short match. */
static inline void xx_decode_copy_words(uint8_t *dst, const uint8_t *src, size_t size)
{
    while (size >= 8U) {
        uint64_t word;
        memcpy(&word, src, sizeof(word));
        memcpy(dst, &word, sizeof(word));
        src += 8U;
        dst += 8U;
        size -= 8U;
    }
    while (size-- != 0U) *dst++ = *src++;
}

static void xx_decode_copy_scalar(uint8_t *dst, const uint8_t *src, size_t size)
{
    xx_decode_copy_words(dst, src, size);
}

/* Seed spans belong to the same ring allocation and can overlap physically
 * even though the logical history ranges are distinct across a ring wrap. */
static inline void xx_decode_move_words(uint8_t *dst, const uint8_t *src, size_t size)
{
    if (dst <= src) {
        xx_decode_copy_words(dst, src, size);
    } else {
        while (size >= 8U) {
            uint64_t word;
            size -= 8U;
            memcpy(&word, src + size, sizeof(word));
            memcpy(dst + size, &word, sizeof(word));
        }
        while (size-- != 0U) dst[size] = src[size];
    }
}

static inline void xx_decode_fill_words(uint8_t *dst, uint8_t value, size_t size)
{
    uint64_t word = (uint64_t)value * UINT64_C(0x0101010101010101);
    while (size >= 8U) {
        memcpy(dst, &word, sizeof(word));
        dst += 8U;
        size -= 8U;
    }
    while (size-- != 0U) *dst++ = value;
}

static void xx_decode_match_scalar(uint8_t *window, size_t win_size, size_t target, size_t source, uint32_t distance, size_t chunk)
{
    size_t seed, first, produced;
    if (distance == 1U) {
        xx_decode_fill_words(window + target, window[source], chunk);
        return;
    }
    seed = chunk < distance ? chunk : distance;
    first = win_size - source;
    if (first > seed) first = seed;
    xx_decode_move_words(window + target, window + source, first);
    if (seed > first) xx_decode_move_words(window + target + first, window, seed - first);
    produced = seed;
    while (produced < chunk) {
        size_t copy = chunk - produced;
        if (copy > produced) copy = produced;
        xx_decode_copy_words(window + target + produced, window + target, copy);
        produced += copy;
    }
}

#ifdef XX_DEFLATE_DEC_X86
XX_DEFLATE_DEC_TARGET_SSE2
static inline void xx_decode_copy_vectors(uint8_t *dst, const uint8_t *src, size_t size)
{
    while (size >= 16U) {
        __m128i value = _mm_loadu_si128((const __m128i *)(const void *)src);
        _mm_storeu_si128((__m128i *)(void *)dst, value);
        src += 16U;
        dst += 16U;
        size -= 16U;
    }
    xx_decode_copy_words(dst, src, size);
}

XX_DEFLATE_DEC_TARGET_SSE2
static XX_DEFLATE_DEC_NOINLINE void xx_decode_copy_sse2(uint8_t *dst, const uint8_t *src, size_t size)
{
    xx_decode_copy_vectors(dst, src, size);
}

XX_DEFLATE_DEC_TARGET_SSE2
static XX_DEFLATE_DEC_NOINLINE void xx_decode_match_sse2(uint8_t *window, size_t win_size, size_t target, size_t source, uint32_t distance, size_t chunk)
{
    uint8_t *dst = window + target;
    if (distance == 1U) {
        uint8_t value = window[source];
        __m128i repeated = _mm_set1_epi8((char)value);
        while (chunk >= 16U) {
            _mm_storeu_si128((__m128i *)(void *)dst, repeated);
            dst += 16U;
            chunk -= 16U;
        }
        xx_decode_fill_words(dst, value, chunk);
    } else if (distance >= 16U) {
        /* Load before store, advancing forwards in stream order. A full
         * vector is no longer than the distance, so it only reads initialized
         * history, including newly generated bytes of overlapping matches. */
        while (chunk != 0U) {
            size_t span = win_size - source;
            if (span > chunk) span = chunk;
            xx_decode_copy_vectors(dst, window + source, span);
            dst += span;
            chunk -= span;
            source = 0U;
        }
    } else {
        /* A short distance cannot safely use a forward vector until its
         * periodic prefix is initialized. Seed, then double disjoint spans. */
        size_t seed = chunk < distance ? chunk : distance;
        size_t first = win_size - source;
        size_t produced = seed;
        if (first > seed) first = seed;
        xx_decode_move_words(dst, window + source, first);
        if (seed > first) xx_decode_move_words(dst + first, window, seed - first);
        while (produced < chunk) {
            size_t copy = chunk - produced;
            if (copy > produced) copy = produced;
            xx_decode_copy_vectors(dst + produced, dst, copy);
            produced += copy;
        }
    }
}
#endif

typedef struct {
    xx_io_device *dev;
    uint8_t *mem_dst;
    size_t mem_cap;
    size_t mem_written;
    uint8_t *buf;
    size_t buf_cap;
    size_t buf_pos;
    int64_t total_written;
    bool error;
    xx_decode_copy_fn copy;
    xx_decode_match_fn match;
} xx_out_acc;

static bool xx_out_init(xx_out_acc *out, xx_io_device *dev, uint8_t *mem_dst, size_t mem_cap)
{
    xx_rt_memset(out, 0, sizeof(*out));
    out->dev = dev;
    out->mem_dst = mem_dst;
    out->mem_cap = mem_cap;
    out->copy = xx_decode_copy_scalar;
    out->match = xx_decode_match_scalar;
#ifdef XX_DEFLATE_DEC_X86
    /* Respect the existing acceleration switch and resolve it once per
     * operation, avoiding CPU-feature dispatch for each short output match. */
    if (xx_is_sse2_enabled()) {
        out->copy = xx_decode_copy_sse2;
        out->match = xx_decode_match_sse2;
    }
#endif

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

static void xx_out_free(xx_out_acc *out)
{
    if (out->buf) {
        xx_mem_free(out->buf);
        out->buf = NULL;
    }
}

static bool xx_out_flush(xx_out_acc *out)
{
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

static inline bool xx_out_put_byte(xx_out_acc *out, uint8_t b)
{
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

static bool xx_out_write(xx_out_acc *out, const uint8_t *data, size_t size)
{
    if (out->error) return false;
    if (out->dev) {
        while (size != 0U) {
            size_t available = out->buf_cap - out->buf_pos;
            size_t chunk;
            if (available == 0U) {
                if (!xx_out_flush(out)) return false;
                available = out->buf_cap;
            }
            chunk = size < available ? size : available;
            out->copy(out->buf + out->buf_pos, data, chunk);
            out->buf_pos += chunk;
            out->total_written += (int64_t)chunk;
            data += chunk;
            size -= chunk;
        }
    } else {
        if (out->mem_dst) {
            if (size > out->mem_cap - out->mem_written) {
                out->error = true;
                return false;
            }
            out->copy(out->mem_dst + out->mem_written, data, size);
            out->mem_written += size;
        }
        out->total_written += (int64_t)size;
    }
    return true;
}

static bool xx_out_copy_match(xx_out_acc *out, uint8_t *window, size_t win_size, size_t *win_pos, uint32_t distance, uint32_t length)
{
    while (length != 0U) {
        size_t target = *win_pos & (win_size - 1U);
        size_t source = (*win_pos - distance) & (win_size - 1U);
        size_t chunk = win_size - target;
        if (chunk > length) chunk = length;
        out->match(window, win_size, target, source, distance, chunk);
        if (!xx_out_write(out, window + target, chunk)) return false;
        *win_pos += chunk;
        length -= (uint32_t)chunk;
    }
    return true;
}

/* ========================================================================= */
/* --- Decompression Engine Core                                         --- */
/* ========================================================================= */

static const uint8_t g_cll_order[XX_DEFLATE_MAX_CLEN_CODES] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

bool xx_deflate_decompress_stream_with_options(xx_bit_reader *reader, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written, bool is_deflate64,
                                               size_t window_size, xx_pd_struct *pd, const uint8_t *dictionary, size_t dictionary_size)
{
    size_t win_size = xx_deflate_resolve_window(is_deflate64, window_size);
    if (!reader || win_size == 0U || (dictionary_size != 0U && !dictionary) || dictionary_size > win_size) return false;
    xx_out_acc out;
    if (!xx_out_init(&out, dst_dev, mem_dst, mem_cap)) {
        return false;
    }

    uint8_t *window = (uint8_t *)xx_mem_alloc(win_size);
    if (!window) {
        xx_out_free(&out);
        return false;
    }
    if ((dictionary_size != 0U && !dictionary) || dictionary_size > win_size) {
        xx_mem_free(window);
        xx_out_free(&out);
        return false;
    }
    if (dictionary_size != 0U) xx_rt_memcpy(window, dictionary, dictionary_size);
    size_t win_pos = dictionary_size;

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

            while (len != 0U) {
                size_t at = win_pos & (win_size - 1U);
                size_t chunk = win_size - at;
                if (chunk > len) chunk = len;
                if (!xx_br_read_bytes(reader, window + at, chunk) || !xx_out_write(&out, window + at, chunk)) {
                    success = false;
                    break;
                }
                win_pos += chunk;
                len = (uint16_t)(len - chunk);
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
                uint32_t hlit = xx_br_read(reader, 5) + 257;
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

                    if (dist == 0U || dist > win_size || dist > win_pos) {
                        success = false;
                        break;
                    }

                    if (!xx_out_copy_match(&out, window, win_size, &win_pos, dist, length)) success = false;
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

    if (reader->error) success = false;
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

bool xx_deflate_decompress_stream_with_dictionary(xx_bit_reader *reader, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written, bool is_deflate64,
                                                  xx_pd_struct *pd, const uint8_t *dictionary, size_t dictionary_size)
{
    return xx_deflate_decompress_stream_with_options(reader, dst_dev, mem_dst, mem_cap, out_written, is_deflate64, 0U, pd, dictionary, dictionary_size);
}

bool xx_deflate_decompress_stream(xx_bit_reader *reader, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written, bool is_deflate64,
                                  xx_pd_struct *pd)
{
    return xx_deflate_decompress_stream_with_dictionary(reader, dst_dev, mem_dst, mem_cap, out_written, is_deflate64, pd, NULL, 0U);
}
