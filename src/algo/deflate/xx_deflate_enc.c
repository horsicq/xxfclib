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

static bool xx_huff_node_less(const xx_huff_node *nodes, int a, int b)
{
    return nodes[a].freq < nodes[b].freq || (nodes[a].freq == nodes[b].freq && a < b);
}

static void xx_huff_heap_push(const xx_huff_node *nodes, int *heap, int *count, int node)
{
    int at = (*count)++;
    while (at != 0) {
        int parent = (at - 1) / 2;
        if (!xx_huff_node_less(nodes, node, heap[parent])) break;
        heap[at] = heap[parent];
        at = parent;
    }
    heap[at] = node;
}

static int xx_huff_heap_pop(const xx_huff_node *nodes, int *heap, int *count)
{
    int result = heap[0];
    int node = heap[--*count];
    int at = 0;
    while (at * 2 + 1 < *count) {
        int child = at * 2 + 1;
        if (child + 1 < *count && xx_huff_node_less(nodes, heap[child + 1], heap[child])) ++child;
        if (!xx_huff_node_less(nodes, heap[child], node)) break;
        heap[at] = heap[child];
        at = child;
    }
    if (*count != 0) heap[at] = node;
    return result;
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

    /* Combine the two lightest nodes using a heap. Index ties preserve the
     * previous scan's deterministic ordering without its quadratic work. */
    int heap[600];
    int active = 0;
    int cur_nodes = num_nodes;
    for (int i = 0; i < num_nodes; ++i) xx_huff_heap_push(nodes, heap, &active, i);

    while (active > 1) {
        int min1 = xx_huff_heap_pop(nodes, heap, &active);
        int min2 = xx_huff_heap_pop(nodes, heap, &active);

        nodes[cur_nodes].freq = nodes[min1].freq + nodes[min2].freq;
        nodes[cur_nodes].symbol = 0xFFFF;
        nodes[cur_nodes].left = (int16_t)min1;
        nodes[cur_nodes].right = (int16_t)min2;

        xx_huff_heap_push(nodes, heap, &active, cur_nodes);
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
        active = 0;
        for (int i = 0; i < num_nodes; ++i) xx_huff_heap_push(nodes, heap, &active, i);
        for (int bits = max_bits; bits > 0; --bits)
            for (unsigned i = 0; i < counts[bits]; ++i) {
                int leaf = xx_huff_heap_pop(nodes, heap, &active);
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

static bool xx_emit_block(xx_bit_writer *bw, const xx_token *tokens, size_t num_tokens, const uint8_t *raw_data, size_t raw_size, bool is_deflate64, bool bfinal)
{
    uint32_t lit_freq[XX_DEFLATE_MAX_LIT_LEN_CODES] = {0};
    uint32_t dist_freq[XX_DEFLATE_MAX_DIST_CODES_64] = {0};
    uint64_t extra_bits = 0U;

    lit_freq[256] = 1; /* End-of-block */

    for (size_t i = 0; i < num_tokens; ++i) {
        lit_freq[tokens[i].symbol]++;
        if (tokens[i].symbol >= 257U) {
            dist_freq[tokens[i].distance_symbol]++;
            extra_bits += tokens[i].length_bits + tokens[i].distance_bits;
        }
    }

    uint8_t lit_lens[XX_DEFLATE_MAX_LIT_LEN_CODES];
    uint8_t dist_lens[XX_DEFLATE_MAX_DIST_CODES_64];
    uint16_t lit_codes[XX_DEFLATE_MAX_LIT_LEN_CODES];
    uint16_t dist_codes[XX_DEFLATE_MAX_DIST_CODES_64];

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
    for (size_t i = 0; i < num_tokens; ++i) {
        const xx_token *token = &tokens[i];
        if (!xx_bw_write_bits(bw, lit_codes[token->symbol], lit_lens[token->symbol])) return false;
        if (token->symbol >= 257U) {
            if (!xx_bw_write_bits(bw, token->length_extra, token->length_bits) ||
                !xx_bw_write_bits(bw, dist_codes[token->distance_symbol], dist_lens[token->distance_symbol]) ||
                !xx_bw_write_bits(bw, token->distance_extra, token->distance_bits))
                return false;
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

static inline uint32_t xx_calc_hash(const uint8_t *p)
{
    return ((((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ (uint32_t)p[2]) & XX_HASH_MASK);
}

bool xx_deflate_compress_stream_with_window(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size, int64_t src_offset, int64_t uncomp_size,
                                            xx_bit_writer *writer, int level, bool is_deflate64, size_t window_size, xx_pd_struct *pd)
{
    size_t win_size = xx_deflate_resolve_window(is_deflate64, window_size);
    if (win_size == 0U) return false;
    if (level < 0) level = XX_DEFLATE_LEVEL_DEFAULT;
    if (level > 9) level = XX_DEFLATE_LEVEL_BEST;

    size_t in_buf_cap = xx_get_file_buffer_size();
    if (in_buf_cap < win_size * 2) {
        in_buf_cap = win_size * 2;
    }

    uint8_t *in_window = (uint8_t *)xx_mem_alloc(in_buf_cap + win_size);
    if (!in_window) {
        return false;
    }

    uint64_t *hash_head = (uint64_t *)xx_mem_alloc(XX_HASH_SIZE * sizeof(uint64_t));
    uint64_t *hash_prev = (uint64_t *)xx_mem_alloc(win_size * sizeof(uint64_t));
    xx_token *tokens = (xx_token *)xx_mem_alloc(XX_ENC_MAX_TOKENS * sizeof(xx_token));

    if (!hash_head || !hash_prev || !tokens) {
        if (in_window) xx_mem_free(in_window);
        if (hash_head) xx_mem_free(hash_head);
        if (hash_prev) xx_mem_free(hash_prev);
        if (tokens) xx_mem_free(tokens);
        return false;
    }

    for (int i = 0; i < XX_HASH_SIZE; ++i) {
        hash_head[i] = UINT64_MAX;
    }

    if (src_dev && src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) {
            xx_mem_free(in_window);
            xx_mem_free(hash_head);
            xx_mem_free(hash_prev);
            xx_mem_free(tokens);
            return false;
        }
    }

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, uncomp_size > 0 ? (uint64_t)uncomp_size : 0, is_deflate64 ? "Compressing Deflate64" : "Compressing Deflate");
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
    uint64_t window_base = 0;
    xx_deflate_match_function match_long = NULL;
    if (xx_is_avx2_enabled()) match_long = xx_deflate_match_avx2;
    else if (xx_is_sse2_enabled()) match_long = xx_deflate_match_sse2;
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

        /* LZ77 Match Finding into Tokens */
        size_t num_tokens = 0;
        size_t block_start_pos = win_head;

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
            uint64_t match_position = hash_head[h];
            hash_prev[position & (win_size - 1U)] = match_position;
            hash_head[h] = position;

            size_t best_len = 0, best_dist = 0, chain_count = 0;
            size_t max_test = available < max_match_len ? available : max_match_len;
            const uint8_t *current = in_window + win_head;
            while (match_position != UINT64_MAX && chain_count++ < max_chain) {
                if (match_position >= position || match_position < window_base || position - match_position > win_size) break;
                size_t distance = (size_t)(position - match_position);
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
                        if (best_len == max_test) break;
                    }
                }
                /* At exactly one window the ring slot belongs to the current
                 * position; all earlier candidates would be out of range. */
                if (distance == win_size) break;
                match_position = hash_prev[match_position & (win_size - 1U)];
            }

            if (best_len >= 3) {
                xx_set_match_token(&tokens[num_tokens], best_len, best_dist, is_deflate64);
                num_tokens++;

                /* Insert intermediate positions into hash table */
                for (size_t k = 1; k < best_len && (win_head + k + 2) < win_tail; ++k) {
                    uint32_t kh = xx_calc_hash(in_window + win_head + k);
                    uint64_t inserted = position + k;
                    hash_prev[inserted & (win_size - 1U)] = hash_head[kh];
                    hash_head[kh] = inserted;
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
        if (!xx_emit_block(writer, tokens, num_tokens, in_window + block_start_pos, win_head - block_start_pos, is_deflate64, is_last_block)) {
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

bool xx_deflate_compress_stream(xx_io_device *src_dev, const uint8_t *mem_src, size_t mem_src_size, int64_t src_offset, int64_t uncomp_size, xx_bit_writer *writer,
                                int level, bool is_deflate64, xx_pd_struct *pd)
{
    return xx_deflate_compress_stream_with_window(src_dev, mem_src, mem_src_size, src_offset, uncomp_size, writer, level, is_deflate64, 0U, pd);
}
