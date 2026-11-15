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
#include "xx_deflate_enc_tables.h"
#include "platforms/xx_deflate_platform.h"
#include <string.h>

/* ========================================================================= */
/* --- Bit Writer Implementation                                         --- */
/* ========================================================================= */

bool xx_bw_init(xx_bit_writer *bw, xx_io_device *dev, uint8_t *mem_dst, size_t mem_cap)
{
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

void xx_bw_free(xx_bit_writer *bw)
{
    if (bw->buffer) {
        xx_mem_free(bw->buffer);
        bw->buffer = NULL;
    }
}

static bool xx_bw_flush_buffer(xx_bit_writer *bw)
{
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

static inline bool xx_bw_put_byte(xx_bit_writer *bw, uint8_t b)
{
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

static inline bool xx_bw_put_word(xx_bit_writer *bw, uint32_t word)
{
    uint8_t *destination;
    if (bw->error) return false;
    if (bw->dev && bw->buffer_cap - bw->buffer_pos >= 4U) {
        destination = bw->buffer + bw->buffer_pos;
        bw->buffer_pos += 4U;
    } else if (bw->mem_dst && bw->mem_cap - bw->mem_written >= 4U) {
        destination = bw->mem_dst + bw->mem_written;
        bw->mem_written += 4U;
    } else {
        for (unsigned i = 0; i < 4U; ++i)
            if (!xx_bw_put_byte(bw, (uint8_t)(word >> (8U * i)))) return false;
        return true;
    }
    destination[0] = (uint8_t)word;
    destination[1] = (uint8_t)(word >> 8);
    destination[2] = (uint8_t)(word >> 16);
    destination[3] = (uint8_t)(word >> 24);
    bw->total_written += 4;
    return true;
}

static inline bool xx_bw_write_bits(xx_bit_writer *bw, uint32_t val, int n)
{
    bw->bit_buf |= ((uint64_t)val & ((UINT64_C(1) << n) - 1U)) << bw->bit_count;
    bw->bit_count += n;

    if (bw->bit_count >= 32) {
        if (!xx_bw_put_word(bw, (uint32_t)bw->bit_buf)) return false;
        bw->bit_buf >>= 32;
        bw->bit_count -= 32;
    }
    return true;
}

static bool xx_bw_align_byte(xx_bit_writer *bw)
{
    while (bw->bit_count > 0) {
        if (!xx_bw_put_byte(bw, (uint8_t)bw->bit_buf)) return false;
        bw->bit_buf >>= 8;
        bw->bit_count -= 8;
    }
    bw->bit_buf = 0;
    bw->bit_count = 0;
    return true;
}

static bool xx_bw_finish(xx_bit_writer *bw)
{
    return xx_bw_align_byte(bw) && xx_bw_flush_buffer(bw);
}

static bool xx_bw_write_bytes(xx_bit_writer *bw, const uint8_t *data, size_t size)
{
    if (bw->error || bw->bit_count != 0) return false;
    if (size == 0U) return true; /* data may be NULL for an empty stored block */
    if (bw->dev) {
        while (size != 0U) {
            size_t available = bw->buffer_cap - bw->buffer_pos;
            size_t chunk;
            if (available == 0U) {
                if (!xx_bw_flush_buffer(bw)) return false;
                available = bw->buffer_cap;
            }
            chunk = size < available ? size : available;
            xx_rt_memcpy(bw->buffer + bw->buffer_pos, data, chunk);
            bw->buffer_pos += chunk;
            bw->total_written += (int64_t)chunk;
            data += chunk;
            size -= chunk;
        }
    } else {
        if (bw->mem_dst) {
            if (size > bw->mem_cap - bw->mem_written) {
                bw->error = true;
                return false;
            }
            xx_rt_memcpy(bw->mem_dst + bw->mem_written, data, size);
            bw->mem_written += size;
        }
        bw->total_written += (int64_t)size;
    }
    return true;
}

/* ========================================================================= */
/* --- Token and Match Structures                                        --- */
/* ========================================================================= */

typedef struct {
    uint16_t symbol; /* Literal <256; otherwise a cached length symbol. */
    uint16_t length_extra;
    uint16_t distance_extra;
    uint8_t length_bits;
    uint8_t distance_symbol;
    uint8_t distance_bits;
} xx_token;

#define XX_ENC_MAX_TOKENS 65536

/* Length and Distance Encoding Helpers */
static inline void xx_set_match_token(xx_token *token, size_t length, size_t distance, bool is_deflate64)
{
    uint32_t minus_one = (uint32_t)distance - 1U;
    unsigned symbol = xx_distance_codes[minus_one < 256U ? minus_one : 256U + (minus_one >> 7)];
    unsigned bits = symbol < 4U ? 0U : symbol / 2U - 1U;
    unsigned base = symbol < 4U ? symbol + 1U : ((2U + (symbol & 1U)) << bits) + 1U;
    if (is_deflate64 && length >= 258U) {
        /* Deflate64 symbol 285 always has 16 extra bits, including length 258. */
        token->symbol = 285;
        token->length_extra = (uint16_t)(length - 3U);
        token->length_bits = 16;
    } else {
        const xx_length_entry *code = &xx_length_codes[length - 3U];
        token->symbol = code->symbol;
        token->length_extra = code->extra;
        token->length_bits = code->bits;
    }
    token->distance_symbol = (uint8_t)symbol;
    token->distance_extra = (uint16_t)(distance - base);
    token->distance_bits = (uint8_t)bits;
}

/* Reverse bits for LSB-first output */
static inline uint32_t xx_reverse_bits(uint32_t val, int bits)
{
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
    int16_t left;
    int16_t right;
} xx_huff_node;

/* Leaf indices ordered by (frequency, index): unique 64-bit keys, insertion
 * sorted in runs of 16 and then merged bottom-up. */
static void xx_huff_sort_leaves(const xx_huff_node *nodes, int count, int *out)
{
    uint64_t buffer_a[XX_DEFLATE_MAX_LIT_LEN_CODES], buffer_b[XX_DEFLATE_MAX_LIT_LEN_CODES];
    uint64_t *from = buffer_a, *to = buffer_b;
    for (int i = 0; i < count; ++i) {
        uint64_t key = ((uint64_t)nodes[i].freq << 16) | (uint64_t)i;
        int at = i;
        while ((at & 15) != 0 && from[at - 1] > key) {
            from[at] = from[at - 1];
            --at;
        }
        from[at] = key;
    }
    for (int width = 16; width < count; width *= 2) {
        for (int lo = 0; lo < count; lo += 2 * width) {
            int mid = lo + width < count ? lo + width : count;
            int hi = lo + 2 * width < count ? lo + 2 * width : count;
            int a = lo, b = mid, o = lo;
            while (a < mid && b < hi) to[o++] = from[a] < from[b] ? from[a++] : from[b++];
            while (a < mid) to[o++] = from[a++];
            while (b < hi) to[o++] = from[b++];
        }
        uint64_t *swap = from;
        from = to;
        to = swap;
    }
    for (int i = 0; i < count; ++i) out[i] = (int)(from[i] & 0xFFFFU);
}

void xx_deflate_build_code_lengths(const uint32_t *freqs, int num_symbols, uint8_t *out_lens, int max_bits)
{
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

    /* Combine the two lightest nodes, ordered by (frequency, node index).
     * Two queues give exactly the order of a priority queue on that key:
     * the leaves sorted once, and the internal nodes, which are created with
     * non-decreasing frequencies and increasing indices (all above the leaf
     * indices, so a leaf wins a frequency tie, as it does in the heap). */
    int sorted[XX_DEFLATE_MAX_LIT_LEN_CODES];
    xx_huff_sort_leaves(nodes, num_nodes, sorted);
    int cur_nodes = num_nodes;
    int next_leaf = 0, next_inner = num_nodes;
    for (int merge = 1; merge < num_nodes; ++merge) {
        int pick[2];
        for (int k = 0; k < 2; ++k) {
            if (next_leaf < num_nodes && (next_inner >= cur_nodes || nodes[sorted[next_leaf]].freq <= nodes[next_inner].freq))
                pick[k] = sorted[next_leaf++];
            else
                pick[k] = next_inner++;
        }
        int min1 = pick[0], min2 = pick[1];

        nodes[cur_nodes].freq = nodes[min1].freq + nodes[min2].freq;
        nodes[cur_nodes].symbol = 0xFFFF;
        nodes[cur_nodes].left = (int16_t)min1;
        nodes[cur_nodes].right = (int16_t)min2;
        cur_nodes++;
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

    /* Clipping deep leaves alone oversubscribes the code space. Split a
     * shorter code and remove one deepest code until the Kraft sum fits,
     * then give the longest codes to the least frequent symbols. */
    unsigned counts[16] = {0};
    uint32_t used = 0, capacity = 1U << max_bits;
    for (int i = 0; i < num_nodes; ++i) {
        unsigned length = out_lens[nodes[i].symbol];
        ++counts[length];
        used += 1U << (max_bits - length);
    }
    if (used > capacity) {
        while (used > capacity) {
            int bits = max_bits - 1;
            while (bits > 0 && counts[bits] == 0U) --bits;
            --counts[bits];
            counts[bits + 1] += 2U;
            --counts[max_bits];
            --used;
        }
        int at = 0;
        for (int bits = max_bits; bits > 0; --bits)
            for (unsigned i = 0; i < counts[bits]; ++i) {
                int leaf = sorted[at++];
                out_lens[nodes[leaf].symbol] = (uint8_t)bits;
            }
    }
}

static void xx_generate_canonical_codes(const uint8_t *lens, int num_symbols, uint16_t *out_codes)
{
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

static bool xx_emit_stored_block(xx_bit_writer *bw, const uint8_t *data, size_t len, bool bfinal)
{
    if (len > 65535U) return false;
    if (!xx_bw_write_bits(bw, bfinal ? 1 : 0, 1)) return false;
    if (!xx_bw_write_bits(bw, 0, 2)) return false; /* BTYPE = 00 */
    if (!xx_bw_align_byte(bw)) return false;

    uint16_t length = (uint16_t)len;
    uint16_t nlength = (uint16_t)~length;
    if (!xx_bw_write_bits(bw, (uint32_t)length | ((uint32_t)nlength << 16), 32)) return false;

    return xx_bw_write_bytes(bw, data, len);
}

static bool xx_emit_stored_blocks(xx_bit_writer *bw, const uint8_t *data, size_t size, bool bfinal)
{
    do {
        size_t chunk = size < 65535U ? size : 65535U;
        if (!xx_emit_stored_block(bw, data, chunk, bfinal && chunk == size)) return false;
        if (chunk != 0U) data += chunk;
        size -= chunk;
    } while (size != 0U);
    return true;
}

static bool xx_code_lengths_valid(const uint8_t *lengths, int count, int max_bits)
{
    uint32_t remaining = 1U << max_bits;
    for (int i = 0; i < count; ++i) {
        if (lengths[i] != 0U) {
            uint32_t used;
            if (lengths[i] > max_bits) return false;
            used = 1U << (max_bits - lengths[i]);
            if (used > remaining) return false;
            remaining -= used;
        }
    }
    return true;
}

static uint64_t xx_final_block_cost(const xx_bit_writer *bw, uint64_t bits, bool bfinal)
{
    if (bfinal) bits += (8U - ((bw->bit_count + (unsigned)(bits & 7U)) & 7U)) & 7U;
    return bits;
}

static const uint8_t g_cll_order[XX_DEFLATE_MAX_CLEN_CODES] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/* Raw byte output for the staged token stream (pending bits are kept by the
 * caller). Same destinations and accounting as xx_bw_put_byte. */
static bool xx_bw_put_raw(xx_bit_writer *bw, const uint8_t *data, size_t size)
{
    if (bw->error) return false;
    if (bw->dev) {
        while (size != 0U) {
            if (bw->buffer_pos >= bw->buffer_cap && !xx_bw_flush_buffer(bw)) return false;
            size_t chunk = bw->buffer_cap - bw->buffer_pos;
            if (chunk > size) chunk = size;
            xx_rt_memcpy(bw->buffer + bw->buffer_pos, data, chunk);
            bw->buffer_pos += chunk;
            bw->total_written += (int64_t)chunk;
            data += chunk;
            size -= chunk;
        }
        return true;
    }
    if (bw->mem_dst) {
        if (bw->mem_cap - bw->mem_written < size) {
            bw->error = true;
            return false;
        }
        xx_rt_memcpy(bw->mem_dst + bw->mem_written, data, size);
        bw->mem_written += size;
    }
    bw->total_written += (int64_t)size;
    return true;
}

#define XX_EMIT_STAGE_TOKENS 384U

static inline void xx_stage_store64(uint8_t *p, uint64_t v)
{
#if defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM64) || defined(__i386__) || defined(__x86_64__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    memcpy(p, &v, sizeof(v));
#else
    for (unsigned i = 0; i < 8U; ++i) p[i] = (uint8_t)(v >> (8U * i));
#endif
}

static bool xx_emit_block(xx_bit_writer *bw, const xx_token *tokens, size_t num_tokens, const uint8_t *raw_data, size_t raw_size, bool is_deflate64, bool bfinal)
{
    uint32_t lit_freq[XX_DEFLATE_MAX_LIT_LEN_CODES] = {0};
    /* One spare counter absorbs literals in the branch-free count below. */
    uint32_t dist_freq[XX_DEFLATE_MAX_DIST_CODES_64 + 1] = {0};
    uint64_t extra_bits = 0U;

    lit_freq[256] = 1; /* End-of-block */

    /* Literal tokens leave the match fields unset; a mask selects them only
     * for matches, so mixed literal/match streams cost no mispredictions. */
    for (size_t i = 0; i < num_tokens; ++i) {
        unsigned symbol = tokens[i].symbol;
        unsigned match = 0U - (unsigned)(symbol >= 257U);
        lit_freq[symbol]++;
        dist_freq[(tokens[i].distance_symbol & match) | (XX_DEFLATE_MAX_DIST_CODES_64 & ~match)]++;
        extra_bits += (unsigned)(tokens[i].length_bits + tokens[i].distance_bits) & match;
    }

    uint8_t lit_lens[XX_DEFLATE_MAX_LIT_LEN_CODES];
    uint8_t dist_lens[XX_DEFLATE_MAX_DIST_CODES_64] = {0};
    uint16_t lit_codes[XX_DEFLATE_MAX_LIT_LEN_CODES];
    uint16_t dist_codes[XX_DEFLATE_MAX_DIST_CODES_64] = {0};

    xx_deflate_build_code_lengths(lit_freq, XX_DEFLATE_MAX_LIT_LEN_CODES, lit_lens, 15);
    int max_dists = is_deflate64 ? XX_DEFLATE_MAX_DIST_CODES_64 : XX_DEFLATE_MAX_DIST_CODES_STD;
    xx_deflate_build_code_lengths(dist_freq, max_dists, dist_lens, 15);

    size_t stored_blocks = raw_size / 65535U + (raw_size % 65535U != 0U);
    if (stored_blocks == 0U) stored_blocks = 1U;
    uint64_t stored_bits = (uint64_t)raw_size * 8U + 35U + ((8U - ((bw->bit_count + 3U) & 7U)) & 7U) + (uint64_t)(stored_blocks - 1U) * 40U;
    uint64_t payload_bits = extra_bits, fixed_bits = 3U + extra_bits;
    for (int i = 0; i < XX_DEFLATE_MAX_LIT_LEN_CODES; ++i) {
        unsigned fixed_length = i <= 143 ? 8U : i <= 255 ? 9U : i <= 279 ? 7U : 8U;
        payload_bits += (uint64_t)lit_freq[i] * lit_lens[i];
        fixed_bits += (uint64_t)lit_freq[i] * fixed_length;
    }
    for (int i = 0; i < max_dists; ++i) {
        payload_bits += (uint64_t)dist_freq[i] * dist_lens[i];
        fixed_bits += (uint64_t)dist_freq[i] * 5U;
    }
    fixed_bits = xx_final_block_cost(bw, fixed_bits, bfinal);
    /* Every dynamic header costs at least 29 bits. If even that bound loses,
     * skip code-length RLE, the third tree, and canonical code generation. */
    if (stored_bits <= fixed_bits && stored_bits <= payload_bits + 29U) return xx_emit_stored_blocks(bw, raw_data, raw_size, bfinal);

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
    xx_deflate_build_code_lengths(clen_freq, XX_DEFLATE_MAX_CLEN_CODES, clen_lens, 7);
    xx_generate_canonical_codes(clen_lens, XX_DEFLATE_MAX_CLEN_CODES, clen_codes);

    int hclen = XX_DEFLATE_MAX_CLEN_CODES;
    while (hclen > 4 && clen_lens[g_cll_order[hclen - 1]] == 0) {
        hclen--;
    }

    uint64_t dynamic_bits = 17U + 3U * (unsigned)hclen + payload_bits;
    bool dynamic_valid = xx_code_lengths_valid(lit_lens, XX_DEFLATE_MAX_LIT_LEN_CODES, 15) && xx_code_lengths_valid(dist_lens, max_dists, 15) &&
                         xx_code_lengths_valid(clen_lens, XX_DEFLATE_MAX_CLEN_CODES, 7);
    for (int i = 0; i < num_rle; ++i) dynamic_bits += clen_lens[rle_syms[i]] + rle_nbits[i];
    dynamic_bits = xx_final_block_cost(bw, dynamic_bits, bfinal);
    if (!dynamic_valid) dynamic_bits = UINT64_MAX;
    if (stored_bits <= fixed_bits && stored_bits <= dynamic_bits) return xx_emit_stored_blocks(bw, raw_data, raw_size, bfinal);

    bool use_fixed = fixed_bits <= dynamic_bits;
    if (use_fixed) {
        for (int i = 0; i < XX_DEFLATE_MAX_LIT_LEN_CODES; ++i) {
            unsigned code;
            if (i <= 143) {
                lit_lens[i] = 8;
                code = (unsigned)i + 48U;
            } else if (i <= 255) {
                lit_lens[i] = 9;
                code = (unsigned)i + 256U;
            } else if (i <= 279) {
                lit_lens[i] = 7;
                code = (unsigned)i - 256U;
            } else {
                lit_lens[i] = 8;
                code = (unsigned)i - 88U;
            }
            lit_codes[i] = (uint16_t)xx_reverse_bits(code, lit_lens[i]);
        }
        for (int i = 0; i < max_dists; ++i) {
            dist_lens[i] = 5;
            dist_codes[i] = (uint16_t)xx_reverse_bits((unsigned)i, 5);
        }
        if (!xx_bw_write_bits(bw, bfinal ? 1U : 0U, 1) || !xx_bw_write_bits(bw, 1U, 2)) return false;
    } else {
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
    }

    /* Token codes were computed once during match finding. */
    /* Register-cached accumulator: the same LSB-first bit sequence as
     * xx_bw_write_bits, flushed 32 bits at a time. After a flush fewer than
     * 32 bits are pending; a literal/length code plus its extra bits adds at
     * most 15 + 16 and a distance code plus extra at most 15 + 14, so the
     * 64-bit buffer never overflows between the two flush points. */
    /* Branch-free variant: the bit stream is the same whatever the flush
     * granularity, so whole bytes are stored unconditionally (8-byte store,
     * advance by bits / 8) into a local staging buffer that is copied out per
     * chunk. Fewer than 8 bits stay pending between the two flush points, so
     * a literal/length code plus extra (<= 15 + 16) or a distance code plus
     * extra (<= 15 + 14) never overflows 64 bits. Literal tokens leave the
     * match fields unset; the mask zeroes their contribution. */
    {
        uint8_t stage[XX_EMIT_STAGE_TOKENS * 8U + 16U];
        uint64_t acc = bw->bit_buf;
        unsigned acc_bits = (unsigned)bw->bit_count;
        uint8_t *out = stage;
        while (acc_bits >= 8U) {
            *out++ = (uint8_t)acc;
            acc >>= 8;
            acc_bits -= 8U;
        }
        size_t i = 0;
        while (i < num_tokens) {
            size_t end = num_tokens - i > XX_EMIT_STAGE_TOKENS ? i + XX_EMIT_STAGE_TOKENS : num_tokens;
            for (; i < end; ++i) {
                const xx_token *token = &tokens[i];
                unsigned symbol = token->symbol;
                uint64_t match = (uint64_t)0 - (uint64_t)(symbol >= 257U);
                unsigned n = lit_lens[symbol];
                acc |= ((uint64_t)lit_codes[symbol] & ((UINT64_C(1) << n) - 1U)) << acc_bits;
                acc_bits += n;
                n = (unsigned)(token->length_bits & match);
                acc |= ((uint64_t)token->length_extra & ((UINT64_C(1) << n) - 1U) & match) << acc_bits;
                acc_bits += n;
                xx_stage_store64(out, acc);
                out += acc_bits >> 3;
                acc = acc >> (acc_bits & ~7U);
                acc_bits &= 7U;
                unsigned dsym = token->distance_symbol & (XX_DEFLATE_MAX_DIST_CODES_64 - 1U);
                n = (unsigned)(dist_lens[dsym] & match);
                acc |= ((uint64_t)dist_codes[dsym] & ((UINT64_C(1) << n) - 1U) & match) << acc_bits;
                acc_bits += n;
                n = (unsigned)(token->distance_bits & match);
                acc |= ((uint64_t)token->distance_extra & ((UINT64_C(1) << n) - 1U) & match) << acc_bits;
                acc_bits += n;
                xx_stage_store64(out, acc);
                out += acc_bits >> 3;
                acc = acc >> (acc_bits & ~7U);
                acc_bits &= 7U;
            }
            if (!xx_bw_put_raw(bw, stage, (size_t)(out - stage))) return false;
            out = stage;
        }
        if (out != stage && !xx_bw_put_raw(bw, stage, (size_t)(out - stage))) return false;
        bw->bit_buf = acc;
        bw->bit_count = (int)acc_bits;
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
#define XX_HASH_EMPTY_AGE UINT32_C(0x80000000)
#define XX_HASH_SWEEP_SPAN (UINT64_C(1) << 30)
/* Level 1 does not insert the inside of matches longer than this, only
 * their last XX_L1_TAIL positions (which start the likely next matches):
 * runs and long repeats stop costing one hash insert per byte, at a ratio
 * cost of about 0.1% on executables. */
#define XX_L1_INSERT_MAX 32U
#define XX_L1_TAIL 4U

static inline uint32_t xx_calc_hash(const uint8_t *p)
{
    return ((((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ (uint32_t)p[2]) & XX_HASH_MASK);
}

/* ------------------------------------------------------------------------- */
/* Level 1 match finder (no chains, libdeflate/zlib-ng style). A
 * multiplicative hash of the next four bytes selects a 32-bit bucket that
 * holds the low 16 bits of the two most recent positions with that hash
 * (newest in the low half). Each candidate is rejected with one 4-byte
 * compare and extended with the shared 8-byte / SIMD compare. A small side
 * table indexed by three bytes recovers near length-3 matches, which matter
 * for executables. After a run of failed lookups the parser steps over
 * several literals at once (not inserted), so incompressible data costs one
 * lookup per few bytes; a match found afterwards grows backwards over the
 * pending literals. A maximal match first retries its own distance (runs).
 * Matches up to XX_L1F_INS_MAX insert every inner position, longer ones
 * their last XX_L1F_TAIL. Blocks cover up to XX_L1_BLOCK input bytes.
 * Distances are taken modulo 2^16: one that lands inside the window and the
 * buffered history addresses real history bytes, so a stale entry only
 * costs a failed compare and nothing needs aging. */
#ifndef XX_L1_BITS
#define XX_L1_BITS 15
#endif
#ifndef XX_L1_H3_BITS
#define XX_L1_H3_BITS 12
#endif
#ifndef XX_L1_H3_DIST
#define XX_L1_H3_DIST 4096U
#endif
#ifndef XX_L1_SKIP_SHIFT
#define XX_L1_SKIP_SHIFT 5
#endif
#ifndef XX_L1_SKIP_MAX
#define XX_L1_SKIP_MAX 32U
#endif
#ifndef XX_L1_BACK
#define XX_L1_BACK 1
#endif
#ifndef XX_L1F_INS_MAX
#define XX_L1F_INS_MAX 8U
#endif
#ifndef XX_L1_BLOCK
#define XX_L1_BLOCK 65536U
#endif
#ifndef XX_L1F_TAIL
#define XX_L1F_TAIL 4U
#endif
#ifndef XX_L1_PREFETCH
#define XX_L1_PREFETCH 1
#endif
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define XX_L1_PREFETCH_T0(p) _mm_prefetch((const char *)(p), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
#define XX_L1_PREFETCH_T0(p) __builtin_prefetch((p))
#else
#define XX_L1_PREFETCH_T0(p) ((void)(p))
#endif
#define XX_L1_BUCKETS ((size_t)1 << XX_L1_BITS)
#if XX_L1_H3_BITS > 0
#define XX_L1_H3_SIZE ((size_t)1 << XX_L1_H3_BITS)
#else
#define XX_L1_H3_SIZE ((size_t)0)
#endif
#define XX_L1_TABLE_BYTES (XX_L1_BUCKETS * sizeof(uint32_t) + XX_L1_H3_SIZE * sizeof(uint16_t))

static XX_DEFLATE_INLINE uint32_t xx_l1_load32(const uint8_t *p)
{
#if defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM64) || defined(__i386__) || defined(__x86_64__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
#else
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
#endif
}

static XX_DEFLATE_INLINE uint32_t xx_l1_hash4(uint32_t v)
{
    return (v * UINT32_C(0x9E3779B1)) >> (32 - XX_L1_BITS);
}

#if XX_L1_H3_BITS > 0
static XX_DEFLATE_INLINE uint32_t xx_l1_hash3(uint32_t v)
{
    return ((v << 8) * UINT32_C(0x9E3779B1)) >> (32 - XX_L1_H3_BITS);
}
#endif

/* Length of a match whose first four bytes are known equal. */
static XX_DEFLATE_INLINE size_t xx_l1_extend(const uint8_t *cur, const uint8_t *cand, size_t limit, xx_deflate_match_function match_long)
{
    /* Most matches end within the inline 16-byte prefix. */
    size_t left = limit - 4U, prefix = left < 16U ? left : 16U;
    size_t length = xx_deflate_match_words(cur + 4U, cand + 4U, prefix);
    if (length == prefix && length < left) {
        left -= length;
        length += match_long && left >= 32U ? match_long(cur + 4U + length, cand + 4U + length, left)
                                            : xx_deflate_match_words(cur + 4U + length, cand + 4U + length, left);
    }
    return 4U + length;
}

/* Long match: only the last XX_L1F_TAIL positions are inserted. Runs hash
 * them all to one bucket, so it is updated in a register. */
static XX_DEFLATE_INLINE void xx_l1_insert_tail(uint32_t *tab, const uint8_t *win, uint64_t base, size_t head, size_t len, size_t tail)
{
    uint32_t *last = NULL, value = 0U;
    for (size_t k = len - XX_L1F_TAIL; k < len && head + k + 4U <= tail; ++k) {
        uint32_t *s = tab + xx_l1_hash4(xx_l1_load32(win + head + k));
        if (s != last) {
            if (last) *last = value;
            last = s;
            value = *s;
        }
        value = (value << 16) | ((uint32_t)(base + head + k) & 0xFFFFU);
    }
    if (last) *last = value;
}

/* Parse one block starting at *head_io; returns the token count. */
static size_t xx_l1_parse(const uint8_t *win, size_t *head_io, size_t tail, uint64_t base, size_t win_size, uint32_t *tab, uint16_t *tab3,
                          xx_token *tokens, xx_deflate_match_function match_long, bool short_filter, bool is_deflate64)
{
    size_t head = *head_io, start = head, n = 0U, misses = 0U, lits = 0U;
    const size_t token_limit = XX_ENC_MAX_TOKENS - 4U - XX_L1_SKIP_MAX;
    while (head < tail && n < token_limit) {
        size_t avail = tail - head;
        if (avail < 4U) {
            tokens[n++].symbol = win[head++];
            ++lits;
            continue;
        }
        const uint8_t *cur = win + head;
        const uint32_t v = xx_l1_load32(cur);
        const uint32_t pos = (uint32_t)(base + head) & 0xFFFFU;
        uint32_t *slot = tab + xx_l1_hash4(v);
        const uint32_t bucket = *slot;
        *slot = (bucket << 16) | pos;
        /* Valid distances are 1..reach. */
        const size_t reach = head < win_size ? head : win_size;
        const size_t limit = avail < 258U ? avail : 258U;
        /* Branch-free screening: an invalid distance is replaced by 0 (the
         * current position, always readable) and masked out of the hit. */
        const size_t d0 = (pos - bucket) & 0xFFFFU, d1 = (pos - (bucket >> 16)) & 0xFFFFU;
        const size_t s0 = d0 - 1U < reach ? d0 : 0U, s1 = d1 - 1U < reach ? d1 : 0U;
        const unsigned hit0 = (s0 != 0U) & (xx_l1_load32(cur - s0) == v);
        const unsigned hit1 = (s1 != 0U) & (xx_l1_load32(cur - s1) == v);
#if XX_L1_H3_BITS > 0
        uint16_t *s3p = tab3 + xx_l1_hash3(v);
        const size_t d3 = (pos - *s3p) & 0xFFFFU;
        *s3p = (uint16_t)pos;
        const size_t s3 = d3 - 1U < (reach < XX_L1_H3_DIST ? reach : XX_L1_H3_DIST) ? d3 : 0U;
        const uint32_t x3 = xx_l1_load32(cur - s3) ^ v;
        const unsigned hit3 = (s3 != 0U) & ((x3 & 0x00FFFFFFU) == 0U);
#else
        const unsigned hit3 = 0U, s3 = 0U, x3 = 0U;
#endif
        size_t best_len = 0U, best_dist = 0U;
        if (hit0 | hit1 | hit3) {
            if (hit0) {
                best_len = xx_l1_extend(cur, cur - d0, limit, match_long);
                best_dist = d0;
            }
            if (hit1 && best_len < limit && cur[best_len] == cur[best_len - d1]) {
                size_t len = xx_l1_extend(cur, cur - d1, limit, match_long);
                if (len > best_len) {
                    best_len = len;
                    best_dist = d1;
                }
            }
            if (best_len == 0U) {
                best_len = x3 == 0U ? xx_l1_extend(cur, cur - s3, limit, match_long) : 3U;
                best_dist = s3;
            }
        }
        if (best_len == 3U && short_filter && best_dist > 4096U) best_len = 0U;

        if (best_len >= 3U) {
#if XX_L1_BACK
            /* Grow the match backwards over the pending literals of this
             * block: recovers bytes stepped over and starts missed by the
             * two-entry buckets. */
            while (lits != 0U && best_len < 258U && head > best_dist && win[head - 1U] == win[head - 1U - best_dist]) {
                --head;
                --cur;
                --lits;
                --n;
                ++best_len;
            }
#endif
#if XX_L1_PREFETCH
            if (head + best_len + 4U <= tail) XX_L1_PREFETCH_T0(tab + xx_l1_hash4(xx_l1_load32(cur + best_len)));
#endif
            xx_set_match_token(&tokens[n++], best_len, best_dist, is_deflate64);
            if (best_len <= XX_L1F_INS_MAX) {
                for (size_t k = 1U; k < best_len && head + k + 4U <= tail; ++k) {
                    uint32_t *s = tab + xx_l1_hash4(xx_l1_load32(cur + k));
                    *s = (*s << 16) | ((uint32_t)(base + head + k) & 0xFFFFU);
                }
            } else {
                xx_l1_insert_tail(tab, win, base, head, best_len, tail);
            }
            head += best_len;
            misses = 0U;
            lits = 0U;
            /* A maximal match continues a run or long repeat: try the same
             * distance first and skip the lookups while it stays maximal. */
            while (best_len == 258U && tail - head >= 258U && head - start < XX_L1_BLOCK && n < token_limit &&
                   xx_l1_load32(win + head) == xx_l1_load32(win + head - best_dist) &&
                   xx_l1_extend(win + head, win + head - best_dist, 258U, match_long) == 258U) {
                xx_set_match_token(&tokens[n++], 258U, best_dist, is_deflate64);
                xx_l1_insert_tail(tab, win, base, head, 258U, tail);
                head += 258U;
            }
        } else {
            size_t step = 1U;
#if XX_L1_SKIP_SHIFT > 0
            step += misses++ >> XX_L1_SKIP_SHIFT;
            if (step > XX_L1_SKIP_MAX) step = XX_L1_SKIP_MAX;
            if (step > avail) step = avail;
#endif
            for (size_t i = 0; i < step; ++i) tokens[n + i].symbol = cur[i];
            n += step;
            head += step;
            lits += step;
        }
        if (head - start >= XX_L1_BLOCK) break;
    }
    *head_io = head;
    return n;
}

/* ------------------------------------------------------------------------- */
/* Levels 4..9: hash-chain finder with zlib-style lazy evaluation
 * (deflate_slow). A match found at position p is held back for one byte; the
 * search at p + 1 only accepts strictly longer matches, starting from the
 * held length as the bar (cheap rejects through the bytes at bar - 1 and
 * bar). The held match is emitted unless the next one is longer, in which
 * case p becomes a literal. Matches at least lazy long are emitted at once
 * (zlib defers them too but then skips the next search: the same parse).
 * Every position is inserted into the chains. Blocks end on a token budget
 * or an input budget, or when the lookahead runs short and the window can be
 * refilled; a held match is resolved before a block ends. */
typedef struct {
    size_t max_chain;   /* candidates per search */
    size_t lazy;        /* defer matches shorter than this; 0 = greedy */
    size_t nice;        /* stop a search at this length; 0 = off */
    size_t good;        /* quarter the budget once this length is reached; 0 = off */
    bool filter;        /* reject length-3 matches beyond distance 4096 */
    size_t max_match;   /* 258, or 65535 for deflate64 */
    size_t token_limit; /* tokens per block */
    size_t input_limit; /* input bytes per block */
    bool split;         /* end blocks where the symbol statistics change */
} xx_lazy_params;

/* Level defaults for levels 4..9 (index level - 4), used when no explicit
 * option is set: chain, lazy, nice, good, filter, split, tokens per block. */
typedef struct {
    uint16_t chain, lazy, nice, good;
    uint8_t filter, split;
    uint16_t block_tokens;
} xx_lazy_level;

#ifndef XX_LAZY_LEVEL_TABLE
#define XX_LAZY_LEVEL_TABLE                                                                                                                     \
    {16, 4, 16, 4, 1, 1, 65000}, {32, 16, 32, 8, 1, 1, 65000}, {128, 16, 32, 8, 1, 1, 65000}, {256, 32, 128, 8, 1, 1, 65000},                  \
        {512, 128, 258, 32, 1, 1, 65000}, {768, 258, 258, 32, 1, 1, 65000}
#endif
static const xx_lazy_level xx_lazy_levels[6] = {XX_LAZY_LEVEL_TABLE};

#ifndef XX_LAZY_BUF_MIN
#define XX_LAZY_BUF_MIN ((size_t)1 << 20)
#endif
#ifndef XX_SPLIT_SEGMENT
#define XX_SPLIT_SEGMENT 512U
#endif
#ifndef XX_SPLIT_HDR_BASE
#define XX_SPLIT_HDR_BASE 100U
#endif
#ifndef XX_SPLIT_HDR_SYM
#define XX_SPLIT_HDR_SYM 4U
#endif
#define XX_LAZY_INPUT_LIMIT ((size_t)1 << 20)

/* Chains hash four bytes, so a chain holds (nearly) only real 4-byte
 * matches; length-3 matches come from a single-entry table of the newest
 * position per 3-byte hash. Both tables share one allocation (hash_head,
 * then hash3) and the aging sweep. */
#ifndef XX_LAZY_HASH_BITS
#define XX_LAZY_HASH_BITS 15
#endif
#ifndef XX_LAZY_H3_BITS
#define XX_LAZY_H3_BITS 14
#endif
#ifndef XX_LAZY_SKIP_SHIFT
#define XX_LAZY_SKIP_SHIFT 6
#endif
#ifndef XX_LAZY_SKIP_MAX
#define XX_LAZY_SKIP_MAX 8U
#endif
#ifndef XX_LAZY_CHAIN_PREFETCH
#define XX_LAZY_CHAIN_PREFETCH 0
#endif
#ifndef XX_LAZY_END4
#define XX_LAZY_END4 1
#endif
#ifndef XX_LAZY_PREFETCH
#define XX_LAZY_PREFETCH 1
#endif
#define XX_LAZY_HEAD_SIZE ((size_t)1 << XX_LAZY_HASH_BITS)
#define XX_LAZY_H3_SIZE ((size_t)1 << XX_LAZY_H3_BITS)

static XX_DEFLATE_INLINE uint32_t xx_lazy_hash4(const uint8_t *p)
{
    return (xx_l1_load32(p) * UINT32_C(0x1E35A7BD)) >> (32 - XX_LAZY_HASH_BITS);
}

static XX_DEFLATE_INLINE uint32_t xx_lazy_hash3(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    return (v * UINT32_C(0x9E3779B1)) >> (32 - XX_LAZY_H3_BITS);
}

/* Longest match at current that is longer than bar (returned unchanged when
 * none is). match_entry is the chain head for this position, already
 * replaced in hash_head; current has at least four readable bytes. */
static XX_DEFLATE_INLINE size_t xx_lazy_search(const uint8_t *in_window, uint64_t window_base, size_t win_size, const uint32_t *hash_prev, uint32_t match_entry,
                                               uint64_t position, const uint8_t *current, size_t bar, size_t max_test, const xx_lazy_params *p,
                                               xx_deflate_match_function match_long, size_t *dist_out)
{
    size_t best_len = bar, chain_count = 0U;
    size_t search_limit = p->max_chain;
    if (best_len >= max_test) return best_len;
    if (p->good && best_len >= p->good) {
        search_limit = p->max_chain / 4U;
        if (search_limit == 0U) search_limit = 1U;
    }
    const size_t nice = p->nice && p->nice < max_test ? p->nice : max_test;
    const uint32_t first = xx_l1_load32(current);
    while (chain_count++ < search_limit) {
        size_t distance = (uint32_t)((uint32_t)position - match_entry);
        if (distance == 0U || distance > win_size) break;
        uint64_t match_position = position - distance;
        if (match_position < window_base) break;
        const uint8_t *candidate = in_window + (size_t)(match_position - window_base);
        match_entry = hash_prev[match_position & (win_size - 1U)];
#if XX_LAZY_CHAIN_PREFETCH
        {
            /* Start loading the next candidate's bytes around best_len. */
            size_t next = (uint32_t)((uint32_t)position - match_entry);
            if (next - 1U < win_size && position - next >= window_base) XX_L1_PREFETCH_T0(in_window + (size_t)(position - next - window_base) + best_len);
        }
#endif
#if XX_LAZY_END4
        /* best_len >= 3: the four bytes ending at best_len, then the first four. */
        if (xx_l1_load32(candidate + best_len - 3U) == xx_l1_load32(current + best_len - 3U) && xx_l1_load32(candidate) == first) {
#else
        if (candidate[best_len] == current[best_len] && candidate[best_len - 1U] == current[best_len - 1U] && xx_l1_load32(candidate) == first) {
#endif
            size_t prefix = max_test < 16U ? max_test : 16U;
            size_t length = 4U + xx_deflate_match_words(current + 4U, candidate + 4U, prefix - 4U);
            if (length == prefix && length < max_test) {
                size_t left = max_test - length;
                length += match_long && left >= 32U ? match_long(current + length, candidate + length, left)
                                                    : xx_deflate_match_words(current + length, candidate + length, left);
            }
            if (length > best_len) {
                best_len = length;
                *dist_out = distance;
                if (best_len >= nice) break;
                if (p->good && best_len >= p->good && search_limit == p->max_chain) {
                    search_limit = p->max_chain / 4U;
                    if (search_limit == 0U) search_limit = 1U;
                }
            }
        }
        if (distance == win_size) break;
    }
    return best_len;
}

/* Insert positions start + 1 .. start + len - 1 into both tables (those with
 * four readable bytes). */
static XX_DEFLATE_INLINE void xx_lazy_insert_match(const uint8_t *in_window, uint32_t *hash_head, uint32_t *hash_prev, size_t win_size, uint64_t window_base,
                                                   size_t start, size_t len, size_t tail)
{
    uint32_t *hash3 = hash_head + XX_LAZY_HEAD_SIZE;
    size_t end = start + len;
    if (end + 3U > tail) end = tail - 3U;
    for (size_t at = start + 1U; at < end; ++at) {
        const uint8_t *q = in_window + at;
        uint32_t h = xx_lazy_hash4(q), position = (uint32_t)(window_base + at);
        hash_prev[position & (win_size - 1U)] = hash_head[h];
        hash_head[h] = position;
        hash3[xx_lazy_hash3(q)] = position;
    }
}

/* Parse one block starting at *head_io; returns the token count. */
static size_t xx_lazy_parse(const uint8_t *in_window, size_t *head_io, size_t tail, bool src_eof, uint64_t window_base, size_t win_size, uint32_t *hash_head,
                            uint32_t *hash_prev, xx_token *tokens, const xx_lazy_params *p, xx_deflate_match_function match_long, bool is_deflate64,
                            uint32_t *seg_tok, size_t *seg_pos, size_t *segs_out)
{
    uint32_t *hash3 = hash_head + XX_LAZY_HEAD_SIZE;
    size_t head = *head_io, start = head, n = 0U;
    size_t prev_len = 0U, prev_dist = 0U, misses = 0U;
    bool pending = false; /* a match of prev_len at head - 1 is held */
    const size_t min_lookahead = p->max_match + 4U;
    const size_t token_limit = p->token_limit;
    /* Segment boundaries for the block splitter: token index and the input
     * position the tokens before it cover (a held match is not emitted). */
    size_t segs = 0U;
    if (seg_tok) {
        seg_tok[0] = 0U;
        seg_pos[0] = start;
    }
    while (head < tail && n < token_limit && head - start < p->input_limit) {
        size_t available = tail - head;
        /* Truncated lookahead: end the block so the window is refilled. */
        if (!src_eof && available < min_lookahead && head > start + 1U) break;
        if (seg_tok && n - seg_tok[segs] >= XX_SPLIT_SEGMENT) {
            ++segs;
            seg_tok[segs] = (uint32_t)n;
            seg_pos[segs] = pending ? head - 1U : head;
        }
        size_t cur_len = 0U, cur_dist = 0U;
        if (available >= 4U) {
            const uint8_t *current = in_window + head;
            uint32_t h = xx_lazy_hash4(current), h3 = xx_lazy_hash3(current);
            uint64_t position = window_base + head;
            uint32_t match_entry = hash_head[h], entry3 = hash3[h3];
#if XX_LAZY_PREFETCH
            /* The tables exceed L1: fetch the next position's slots early. */
            if (available >= 5U) {
                XX_L1_PREFETCH_T0(hash_head + xx_lazy_hash4(current + 1U));
                XX_L1_PREFETCH_T0(hash3 + xx_lazy_hash3(current + 1U));
            }
#endif
            hash_prev[position & (win_size - 1U)] = match_entry;
            hash_head[h] = (uint32_t)position;
            hash3[h3] = (uint32_t)position;
            size_t max_test = available < p->max_match ? available : p->max_match;
            size_t bar = pending ? prev_len : 3U;
            cur_len = xx_lazy_search(in_window, window_base, win_size, hash_prev, match_entry, position, current, bar, max_test, p, match_long, &cur_dist);
            if (cur_len <= bar) {
                cur_len = 0U;
                /* No 4-byte match: try the newest 3-byte candidate. */
                if (!pending) {
                    size_t d3 = (uint32_t)((uint32_t)position - entry3);
                    if (d3 != 0U && d3 <= win_size && (!p->filter || d3 <= 4096U) && position - d3 >= window_base) {
                        const uint8_t *c3 = current - d3;
                        if (c3[0] == current[0] && c3[1] == current[1] && c3[2] == current[2]) {
                            cur_len = 3U;
                            cur_dist = d3;
                        }
                    }
                }
            }
        }
        if (pending) {
            if (cur_len <= prev_len) {
                /* Emit the held match; position head is already inserted. */
                xx_set_match_token(&tokens[n++], prev_len, prev_dist, is_deflate64);
                xx_lazy_insert_match(in_window, hash_head, hash_prev, win_size, window_base, head, prev_len - 1U, tail);
                head += prev_len - 1U;
                pending = false;
                misses = 0U;
                continue;
            }
            tokens[n++].symbol = in_window[head - 1U];
        }
        if (cur_len >= 3U) {
#if XX_LAZY_SKIP_SHIFT > 0
            if (misses >= ((size_t)1 << XX_LAZY_SKIP_SHIFT)) {
                /* Leaving a skipped stretch (its last misses tokens are
                 * literals, nothing is held): grow the match backwards over
                 * them, staying inside the current split segment. */
                const size_t orig = head, orig_len = cur_len, floor = seg_tok ? seg_tok[segs] : 0U;
                size_t back = 0U;
                while (back < misses && n - back > floor && cur_len < p->max_match && head > cur_dist && in_window[head - 1U] == in_window[head - 1U - cur_dist]) {
                    --head;
                    ++cur_len;
                    ++back;
                }
                if (back) {
                    n -= back;
                    xx_set_match_token(&tokens[n++], cur_len, cur_dist, is_deflate64);
                    xx_lazy_insert_match(in_window, hash_head, hash_prev, win_size, window_base, orig, orig_len, tail);
                    head += cur_len;
                    misses = 0U;
                    continue;
                }
            }
#endif
            misses = 0U;
            if (cur_len < p->lazy && cur_len < p->max_match) {
                pending = true;
                prev_len = cur_len;
                prev_dist = cur_dist;
                ++head;
            } else {
                xx_set_match_token(&tokens[n++], cur_len, cur_dist, is_deflate64);
                xx_lazy_insert_match(in_window, hash_head, hash_prev, win_size, window_base, head, cur_len, tail);
                head += cur_len;
                pending = false;
            }
        } else {
            /* No match here (and nothing held). After a long run of misses
             * step over several literals without inserting them, so data
             * without matches costs fewer lookups. */
            size_t step = 1U;
#if XX_LAZY_SKIP_SHIFT > 0
            step += misses++ >> XX_LAZY_SKIP_SHIFT;
            if (step > XX_LAZY_SKIP_MAX) step = XX_LAZY_SKIP_MAX;
            if (step > available) step = available;
            if (step > token_limit - n) step = token_limit - n;
#endif
            for (size_t i = 0; i < step; ++i) tokens[n + i].symbol = in_window[head + i];
            n += step;
            head += step;
            pending = false;
        }
    }
    if (pending) {
        /* The loop stopped before inserting head. */
        xx_set_match_token(&tokens[n++], prev_len, prev_dist, is_deflate64);
        xx_lazy_insert_match(in_window, hash_head, hash_prev, win_size, window_base, head - 1U, prev_len, tail);
        head += prev_len - 1U;
    }
    if (seg_tok) {
        if (segs && seg_tok[segs] == n) --segs;
        ++segs;
        seg_tok[segs] = (uint32_t)n;
        seg_pos[segs] = head;
        *segs_out = segs;
    }
    *head_io = head;
    return n;
}

/* ------------------------------------------------------------------------- */
/* Block splitter. The parse records a boundary every XX_SPLIT_SEGMENT
 * tokens; adjacent pieces are merged greedily (best saving first) while the
 * estimated cost of one block - entropy of its literal/length and distance
 * symbols plus a header estimate - is below that of two. Extra bits do not
 * depend on the split and are left out. Integer log2 keeps the result the
 * same on every platform. */
#define XX_SPLIT_HIST (XX_DEFLATE_MAX_LIT_LEN_CODES + XX_DEFLATE_MAX_DIST_CODES_64)
#define XX_SPLIT_MAX_SEGS (XX_ENC_MAX_TOKENS / XX_SPLIT_SEGMENT + 2U)

static const uint32_t xx_log2_frac[33] = {0,     2909,  5732,  8473,  11136, 13727, 16248, 18704, 21098, 23433, 25711, 27936, 30109, 32234, 34312, 36346, 38336,
                                          40286, 42196, 44068, 45904, 47705, 49472, 51207, 52911, 54584, 56229, 57845, 59434, 60997, 62534, 64047, 65536};

/* log2(x) in 16.16 fixed point, x >= 1. */
static uint32_t xx_log2_q16(uint32_t x)
{
    unsigned e = 0U;
    uint32_t v = x, m;
    if (v >= (UINT32_C(1) << 16)) { v >>= 16; e += 16U; }
    if (v >= (UINT32_C(1) << 8)) { v >>= 8; e += 8U; }
    if (v >= (UINT32_C(1) << 4)) { v >>= 4; e += 4U; }
    if (v >= (UINT32_C(1) << 2)) { v >>= 2; e += 2U; }
    if (v >= (UINT32_C(1) << 1)) e += 1U;
    m = (e >= 16U ? x >> (e - 16U) : x << (16U - e)) - 65536U;
    return (e << 16) + xx_log2_frac[m >> 11] + (((xx_log2_frac[(m >> 11) + 1U] - xx_log2_frac[m >> 11]) * (m & 2047U)) >> 11);
}

#define XX_SPLIT_LOG_TABLE 1024U

/* Estimated block cost (16.16 bits) of the histogram x + y (y may be NULL):
 * entropy of the literal/length (plus one end-of-block) and distance
 * symbols, plus the header estimate. lg holds log2 in 16.16 for small
 * counts. */
static uint64_t xx_split_cost(const uint32_t *x, const uint32_t *y, const uint32_t *lg)
{
    uint64_t cost = (uint64_t)XX_SPLIT_HDR_BASE << 16;
    for (int part = 0; part < 2; ++part) {
        const size_t from = part ? XX_DEFLATE_MAX_LIT_LEN_CODES : 0U, to = part ? XX_SPLIT_HIST : XX_DEFLATE_MAX_LIT_LEN_CODES;
        uint64_t total = part ? 0U : 1U, sum = 0U; /* end-of-block: count 1, log2 0 */
        unsigned used = part ? 0U : 1U;
        for (size_t i = from; i < to; ++i) {
            uint32_t f = x[i] + (y ? y[i] : 0U);
            if (f) {
                total += f;
                sum += (uint64_t)f * (f < XX_SPLIT_LOG_TABLE ? lg[f] : xx_log2_q16(f));
                ++used;
            }
        }
        if (total) cost += total * xx_log2_q16((uint32_t)total) - sum + ((uint64_t)used * XX_SPLIT_HDR_SYM << 16);
    }
    return cost;
}

/* Merge segments [0, segs) into blocks; returns the block count and leaves
 * each block's first segment index in first[] (first[count] = segs). hist
 * holds segs histograms of XX_SPLIT_HIST counters. */
static size_t xx_split_blocks(const xx_token *tokens, const uint32_t *seg_tok, size_t segs, uint32_t *hist, size_t *first)
{
    uint64_t cost[XX_SPLIT_MAX_SEGS];
    int64_t gain[XX_SPLIT_MAX_SEGS]; /* saving of merging block b with block b + 1 */
    uint32_t lg[XX_SPLIT_LOG_TABLE];
    size_t blocks = segs;
    lg[0] = 0U;
    for (uint32_t f = 1U; f < XX_SPLIT_LOG_TABLE; ++f) lg[f] = xx_log2_q16(f);
    xx_rt_memset(hist, 0, segs * XX_SPLIT_HIST * sizeof(uint32_t));
    for (size_t b = 0; b < segs; ++b) {
        uint32_t *h = hist + b * XX_SPLIT_HIST;
        for (size_t i = seg_tok[b]; i < seg_tok[b + 1U]; ++i) {
            unsigned symbol = tokens[i].symbol;
            ++h[symbol];
            if (symbol >= 257U) ++h[XX_DEFLATE_MAX_LIT_LEN_CODES + tokens[i].distance_symbol];
        }
        cost[b] = xx_split_cost(h, NULL, lg);
        first[b] = b;
    }
    first[segs] = segs;
    for (size_t b = 0; b + 1U < blocks; ++b)
        gain[b] = (int64_t)(cost[b] + cost[b + 1U]) - (int64_t)xx_split_cost(hist + first[b] * XX_SPLIT_HIST, hist + first[b + 1U] * XX_SPLIT_HIST, lg);
    while (blocks > 1U) {
        size_t best = 0U;
        for (size_t b = 1; b + 1U < blocks; ++b)
            if (gain[b] > gain[best]) best = b;
        if (gain[best] <= 0) break;
        /* Merge best + 1 into best. */
        uint32_t *x = hist + first[best] * XX_SPLIT_HIST;
        const uint32_t *y = hist + first[best + 1U] * XX_SPLIT_HIST;
        for (size_t i = 0; i < XX_SPLIT_HIST; ++i) x[i] += y[i];
        cost[best] = cost[best] + cost[best + 1U] - (uint64_t)gain[best];
        for (size_t b = best + 1U; b + 1U < blocks; ++b) {
            first[b] = first[b + 1U];
            cost[b] = cost[b + 1U];
            gain[b - 1U] = gain[b];
        }
        first[blocks - 1U] = first[blocks];
        --blocks;
        for (size_t b = best ? best - 1U : 0U; b <= best && b + 1U < blocks; ++b)
            gain[b] = (int64_t)(cost[b] + cost[b + 1U]) - (int64_t)xx_split_cost(hist + first[b] * XX_SPLIT_HIST, hist + first[b + 1U] * XX_SPLIT_HIST, lg);
    }
    return blocks;
}

bool xx_deflate_compress_stream_with_options_ex(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size, int64_t src_offset, int64_t uncomp_size,
                                             xx_bit_writer *writer, int level, bool is_deflate64, size_t window_size,
                                             const xx_deflate_pack_options_ex *options, xx_pd_struct *pd)
{
    size_t win_size = xx_deflate_resolve_window(is_deflate64, window_size);
    if (win_size == 0U) return false;
    size_t requested_candidates = options ? options->max_candidates : 0U;
    size_t lazy_limit = options ? options->lazy_match_limit : 0U;
    if (requested_candidates != 0U || level < 0) level = XX_DEFLATE_LEVEL_DEFAULT;
    if (level > 9) level = XX_DEFLATE_LEVEL_BEST;
    /* Levels 4..9 use the lazy parser. Without explicit options (NULL or an
     * all-zero struct) every knob comes from xx_lazy_levels; once any option
     * is set, all five keep their documented meaning (zero lazy = greedy,
     * zero nice/good = off, the filter as given) and only a zero candidate
     * count takes the level's chain. Levels 1..3 keep the old parser. */
    const bool lazy_path = level >= 4;
    const bool explicit_options = options && (options->max_candidates || options->lazy_match_limit || options->short_match_filter || options->nice_match ||
                                              options->good_match);
    xx_lazy_params lazy_params = {0};
    if (lazy_path) {
        const xx_lazy_level *lv = &xx_lazy_levels[level - 4];
        lazy_params.max_chain = requested_candidates ? requested_candidates : lv->chain;
        lazy_params.lazy = explicit_options ? lazy_limit : lv->lazy;
        lazy_params.nice = explicit_options ? options->nice_match : lv->nice;
        lazy_params.good = explicit_options ? options->good_match : lv->good;
        lazy_params.filter = explicit_options ? options->short_match_filter : lv->filter != 0U;
        lazy_params.max_match = is_deflate64 ? 65535U : 258U;
        lazy_params.token_limit = lv->block_tokens;
        lazy_params.input_limit = XX_LAZY_INPUT_LIMIT;
        lazy_params.split = lv->split != 0U;
    }

    size_t in_buf_cap = xx_get_file_buffer_size();
    if (in_buf_cap < win_size * 2) {
        in_buf_cap = win_size * 2;
    }
    /* The lazy parser keeps max_match + 2 bytes of lookahead and ends a block
     * when it runs short, so it reads in larger chunks. */
    if (lazy_path && in_buf_cap < XX_LAZY_BUF_MIN) in_buf_cap = XX_LAZY_BUF_MIN;
    if (lazy_path && in_buf_cap < win_size * 4) in_buf_cap = win_size * 4;
    const size_t fill_target = lazy_path ? in_buf_cap : win_size;

    uint8_t *in_window = (uint8_t *)xx_mem_alloc(in_buf_cap + win_size);
    if (!in_window) {
        return false;
    }

    /* Hash tables keep the low 32 bits of absolute positions. A live entry is
     * always less than 2^31 behind the current position (see the periodic
     * sweep below), so the distance (uint32_t)position - entry is exact and
     * the chain walk takes the same decisions as with 64-bit positions while
     * the working set is halved. Empty slots hold an entry 2^31 behind. */
    uint32_t *hash_head = NULL;
    uint32_t *hash_prev = NULL;
    uint32_t *l1_table = NULL;
    xx_token *tokens = NULL;
    uint32_t *split_hist = NULL;
    uint32_t split_tok[XX_SPLIT_MAX_SEGS + 1U];
    size_t split_pos[XX_SPLIT_MAX_SEGS + 1U], split_first[XX_SPLIT_MAX_SEGS + 1U], split_segs = 0U;
    /* Level 1 without lazy matching uses the chainless finder (xx_l1_parse)
     * and its own 16-bit bucket table instead of hash_head/hash_prev. */
    const bool l1_fast = level == 1 && lazy_limit == 0U;
    /* The lazy parser's 4-byte chain heads are followed by its 3-byte table. */
    const size_t head_entries = lazy_path ? XX_LAZY_HEAD_SIZE + XX_LAZY_H3_SIZE : (size_t)XX_HASH_SIZE;
    if (level != XX_DEFLATE_LEVEL_STORED) {
        if (l1_fast) {
            l1_table = (uint32_t *)xx_mem_alloc(XX_L1_TABLE_BYTES);
            if (l1_table) xx_rt_memset(l1_table, 0, XX_L1_TABLE_BYTES);
        } else {
            hash_head = (uint32_t *)xx_mem_alloc(head_entries * sizeof(uint32_t));
            hash_prev = (uint32_t *)xx_mem_alloc(win_size * sizeof(uint32_t));
        }
        tokens = (xx_token *)xx_mem_alloc(XX_ENC_MAX_TOKENS * sizeof(xx_token));
        if (lazy_path && lazy_params.split) {
            /* Splitter work area; without it blocks are just not split. */
            split_hist = (uint32_t *)xx_mem_alloc(XX_SPLIT_MAX_SEGS * XX_SPLIT_HIST * sizeof(uint32_t));
        }
        if ((l1_fast ? !l1_table : (!hash_head || !hash_prev)) || !tokens) {
            xx_mem_free(in_window);
            if (hash_head) xx_mem_free(hash_head);
            if (hash_prev) xx_mem_free(hash_prev);
            if (l1_table) xx_mem_free(l1_table);
            if (tokens) xx_mem_free(tokens);
            return false;
        }
        if (hash_head)
            for (size_t i = 0; i < head_entries; ++i) hash_head[i] = XX_HASH_EMPTY_AGE;
    }

    if (src_dev && src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) {
            xx_mem_free(in_window);
            if (hash_head) xx_mem_free(hash_head);
            if (hash_prev) xx_mem_free(hash_prev);
            if (l1_table) xx_mem_free(l1_table);
            if (tokens) xx_mem_free(tokens);
            return false;
        }
    }

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, uncomp_size > 0 ? (uint64_t)uncomp_size : 0, is_deflate64 ? "Compressing Deflate64" : "Compressing Deflate");
    }

    size_t max_chain = requested_candidates ? requested_candidates : (level <= 3 ? 4U : (level <= 6 ? 16U : 128U));
    size_t max_match_len = is_deflate64 ? 65535 : 258;
    /* After the normalisation above, level 1 implies no requested candidate
     * count; levels 2..9 keep their output unchanged. */
    const bool fast1 = level == 1;
    if (max_match_len > 258 && level <= 3) {
        max_match_len = 258;
    }

    int64_t remaining = uncomp_size;
    int64_t processed = 0;
    size_t win_head = 0; /* Current valid data start in in_window */
    size_t win_tail = 0; /* Current valid data end in in_window */
    size_t mem_pos = 0;
    uint64_t window_base = 0;
    uint64_t last_sweep = 0;
    xx_deflate_match_function match_long = NULL;
    if (xx_is_avx2_enabled()) match_long = xx_deflate_match_avx2;
    else if (xx_is_sse2_enabled()) match_long = xx_deflate_match_sse2;
#ifdef XX_DEFLATE_NEON
    else if (xx_is_neon_enabled()) match_long = xx_deflate_match_neon;
#endif
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
            /* Chains store absolute positions in a circular history table.
             * Moving the input bytes no longer rebases either hash table. */
            window_base += shift;
            win_head -= shift;
            win_tail -= shift;
        }

        /* Read more data into window */
        while (!src_eof && (win_tail - win_head) < fill_target && win_tail < in_buf_cap + win_size) {
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
                    if (n_read < 0 || remaining > 0) success = false;
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

        if (!success || win_head >= win_tail) {
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

        /* Keep every stored entry within 2^31 of the current position: an
         * entry farther than one window is never usable, so it is replaced by
         * the empty marker (exactly 2^31 behind). Between sweeps (2^30 input
         * bytes) no entry can age past 2^31 + 2^30, so nothing wraps. */
        if (hash_head && window_base + win_head - last_sweep >= XX_HASH_SWEEP_SPAN) {
            uint32_t now = (uint32_t)(window_base + win_head);
            last_sweep = window_base + win_head;
            for (size_t i = 0; i < head_entries; ++i)
                if ((uint32_t)(now - hash_head[i]) > win_size) hash_head[i] = now - XX_HASH_EMPTY_AGE;
            for (size_t i = 0; i < win_size; ++i)
                if ((uint32_t)(now - hash_prev[i]) > win_size) hash_prev[i] = now - XX_HASH_EMPTY_AGE;
        }

        /* LZ77 Match Finding into Tokens */
        size_t num_tokens = 0;
        size_t block_start_pos = win_head;

        if (l1_fast)
            num_tokens = xx_l1_parse(in_window, &win_head, win_tail, window_base, win_size, l1_table, (uint16_t *)(l1_table + XX_L1_BUCKETS), tokens, match_long,
                                     options && options->short_match_filter, is_deflate64);
        else if (lazy_path)
            num_tokens = xx_lazy_parse(in_window, &win_head, win_tail, src_eof, window_base, win_size, hash_head, hash_prev, tokens, &lazy_params, match_long,
                                       is_deflate64, split_hist ? split_tok : NULL, split_pos, &split_segs);
        else
        while (win_head < win_tail && num_tokens < XX_ENC_MAX_TOKENS - 4) {
            size_t available = win_tail - win_head;
            if (available < 3) {
                /* Not enough bytes for match, emit literal */
                tokens[num_tokens].symbol = in_window[win_head++];
                num_tokens++;
                continue;
            }

            uint32_t h = xx_calc_hash(in_window + win_head);
            uint64_t position = window_base + win_head;
            uint32_t match_entry = hash_head[h];
            uint64_t match_position;
            hash_prev[position & (win_size - 1U)] = match_entry;
            hash_head[h] = (uint32_t)position;

            size_t best_len = 0, best_dist = 0, chain_count = 0;
            size_t search_limit = max_chain;
            size_t nice_match = options ? options->nice_match : 0U;
            size_t good_match = options ? options->good_match : 0U;
            size_t max_test = available < max_match_len ? available : max_match_len;
            const uint8_t *current = in_window + win_head;
            while (chain_count++ < search_limit) {
                size_t distance = (uint32_t)((uint32_t)position - match_entry);
                if (distance == 0U || distance > win_size) break;
                match_position = position - distance;
                if (match_position < window_base) break;
                const uint8_t *candidate = in_window + (size_t)(match_position - window_base);
                if (candidate[best_len] == current[best_len] && candidate[0] == current[0]) {
                    size_t prefix = max_test < 16U ? max_test : 16U;
                    size_t length = xx_deflate_match_words(current, candidate, prefix);
                    if (length == prefix && length < max_test) {
                        size_t left = max_test - length;
                        length += match_long && left >= 32U ? match_long(current + length, candidate + length, left)
                                                            : xx_deflate_match_words(current + length, candidate + length, left);
                    }
                    if (length > best_len) {
                        best_len = length;
                        best_dist = distance;
                        if (best_len == max_test || (nice_match && best_len >= 3U && best_len >= nice_match)) break;
                        if (good_match && best_len >= 3U && best_len >= good_match) {
                            /* Reduce the total budget once, not once per improvement. */
                            search_limit = max_chain / 4U;
                            if (search_limit == 0U) search_limit = 1U;
                        }
                    }
                }
                /* At exactly one window the ring slot belongs to the current
                 * position; all earlier candidates would be out of range. */
                if (distance == win_size) break;
                match_entry = hash_prev[match_position & (win_size - 1U)];
            }

            /* Distant length-3 matches can cost more bits than literals. */
            if (options && options->short_match_filter && best_len == 3U && best_dist > 4096U) best_len = 0U;

            /* One-byte lazy parsing: only prove that a longer next match
             * exists, then let the normal next iteration choose its best
             * distance. Do not mutate the next position's hash chains here.
             * Prefix comparisons are bounded by initialized input, with the
             * same SIMD comparator as ordinary match finding for long spans. */
            if (best_len >= 3U && best_len < lazy_limit && best_len < max_match_len && available > best_len + 1U) {
                size_t needed = best_len + 1U, searched = 0U;
                uint64_t next_position = position + 1U;
                uint32_t next_entry = hash_head[xx_calc_hash(current + 1U)];
                uint64_t next_match;
                while (searched++ < max_chain) {
                    size_t next_distance = (uint32_t)((uint32_t)next_position - next_entry);
                    if (next_distance == 0U || next_distance > win_size) break;
                    next_match = next_position - next_distance;
                    if (next_match < window_base) break;
                    const uint8_t *candidate = in_window + (size_t)(next_match - window_base);
                    if (candidate[best_len] == current[1U + best_len] && candidate[0] == current[1]) {
                        size_t prefix = needed < 16U ? needed : 16U;
                        size_t length = xx_deflate_match_words(current + 1U, candidate, prefix);
                        if (length == prefix && length < needed) {
                            size_t left = needed - length;
                            length += match_long && left >= 32U ? match_long(current + 1U + length, candidate + length, left)
                                                               : xx_deflate_match_words(current + 1U + length, candidate + length, left);
                        }
                        if (length == needed) { best_len = 0U; break; }
                    }
                    if (next_position - next_match == win_size) break;
                    next_entry = hash_prev[next_match & (win_size - 1U)];
                }
            }

            if (best_len >= 3) {
                xx_set_match_token(&tokens[num_tokens], best_len, best_dist, is_deflate64);
                num_tokens++;

                /* Insert intermediate positions into hash table */
                size_t k = 1;
                if (fast1 && best_len > XX_L1_INSERT_MAX) k = best_len - XX_L1_TAIL;
                for (; k < best_len && (win_head + k + 2) < win_tail; ++k) {
                    uint32_t kh = xx_calc_hash(in_window + win_head + k);
                    uint64_t inserted = position + k;
                    hash_prev[inserted & (win_size - 1U)] = hash_head[kh];
                    hash_head[kh] = (uint32_t)inserted;
                }
                win_head += best_len;
            } else {
                tokens[num_tokens].symbol = in_window[win_head++];
                num_tokens++;
            }

            /* Block size threshold: emit block every ~32KB to 64KB input */
            if ((win_head - block_start_pos) >= 32768) {
                break;
            }
        }

        bool is_last_block = (src_eof && win_head >= win_tail);
        if (split_hist && lazy_path && split_segs > 1U) {
            size_t blocks = xx_split_blocks(tokens, split_tok, split_segs, split_hist, split_first);
            for (size_t b = 0; b < blocks && success; ++b) {
                size_t s0 = split_first[b], s1 = split_first[b + 1U];
                if (!xx_emit_block(writer, tokens + split_tok[s0], split_tok[s1] - split_tok[s0], in_window + split_pos[s0], split_pos[s1] - split_pos[s0],
                                   is_deflate64, is_last_block && b + 1U == blocks))
                    success = false;
            }
            if (!success) break;
        } else if (!xx_emit_block(writer, tokens, num_tokens, in_window + block_start_pos, win_head - block_start_pos, is_deflate64, is_last_block)) {
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
    if (hash_head) xx_mem_free(hash_head);
    if (hash_prev) xx_mem_free(hash_prev);
    if (l1_table) xx_mem_free(l1_table);
    if (tokens) xx_mem_free(tokens);
    if (split_hist) xx_mem_free(split_hist);

    return success;
}

bool xx_deflate_compress_stream_with_options(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size, int64_t src_offset, int64_t uncomp_size,
                                              xx_bit_writer *writer, int level, bool is_deflate64, size_t window_size,
                                              const xx_deflate_pack_options *options, xx_pd_struct *pd)
{
    xx_deflate_pack_options_ex extended = {options ? options->max_candidates : 0U, 0U, false, 0U, 0U};
    return xx_deflate_compress_stream_with_options_ex(src_dev, mem_src, mem_src_size, src_offset, uncomp_size, writer, level, is_deflate64, window_size, &extended, pd);
}

bool xx_deflate_compress_stream_with_window(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size, int64_t src_offset, int64_t uncomp_size,
                                            xx_bit_writer *writer, int level, bool is_deflate64, size_t window_size, xx_pd_struct *pd)
{
    return xx_deflate_compress_stream_with_options(src_dev, mem_src, mem_src_size, src_offset, uncomp_size, writer, level, is_deflate64, window_size, NULL, pd);
}

bool xx_deflate_compress_stream(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size, int64_t src_offset, int64_t uncomp_size, xx_bit_writer *writer,
                                int level, bool is_deflate64, xx_pd_struct *pd)
{
    return xx_deflate_compress_stream_with_window(src_dev, mem_src, mem_src_size, src_offset, uncomp_size, writer, level, is_deflate64, 0U, pd);
}
