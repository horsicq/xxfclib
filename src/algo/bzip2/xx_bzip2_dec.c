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

/* Bzip2 decompressor — clean C implementation.
 *
 * Algorithm: Burrows-Wheeler Transform (BWT) + Move-to-Front (MTF) +
 * run-length encoding + canonical Huffman coding, as defined by the bzip2
 * format specification (Julian Seward, 1996-2019).
 *
 * This implementation is an independent rewrite using the same algorithm
 * with xx_io_device I/O and xxfclib memory primitives.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_bzip2_internal.h"
#include "xx_bzip2_random.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#include "../../io/xx_io_buffer.h"
#include <string.h>

extern uint32_t xx_crc32_mpeg2_raw(uint32_t reg, const void *data, size_t size);

/* =========================================================================
 * Bit-reader helpers
 * ========================================================================= */

static bool bz2_br_refill(bz2_bit_reader *br)
{
    if (br->error || br->eof) return false;
    if (br->ibuf_pos < br->ibuf_len) return true;
    if (!br->dev) {
        br->eof = true;
        return false;
    }

    size_t want = br->ibuf_capacity;
    if (br->remaining >= 0) {
        if (br->remaining == 0) {
            br->eof = true;
            return false;
        }
        if ((int64_t)want > br->remaining) want = (size_t)br->remaining;
    }

    ssize_t got = xx_io_read(br->dev, br->ibuf, want);
    if (got <= 0) {
        br->eof = true;
        return false;
    }
    if (br->remaining >= 0) br->remaining -= got;
    br->ibuf_pos = 0;
    br->ibuf_len = (size_t)got;
    return true;
}

bool bz2_br_init(bz2_bit_reader *br, xx_io_device *dev, const uint8_t *mem, size_t mem_size, int64_t remaining)
{
    xx_rt_memset(br, 0, sizeof(*br));
    br->dev = dev;
    br->mem = mem;
    br->mem_size = mem_size;
    br->remaining = remaining;
    if (!dev && mem) {
        br->ibuf = (uint8_t *)mem;
        br->ibuf_capacity = mem_size;
        br->ibuf_pos = 0;
        br->ibuf_len = (remaining >= 0 && (size_t)remaining < mem_size) ? (size_t)remaining : mem_size;
        br->mem_pos = br->ibuf_len;
        br->error = false;
    } else {
        br->ibuf_capacity = xx_get_file_buffer_size();
        if (br->ibuf_capacity < (256 * 1024)) br->ibuf_capacity = (256 * 1024);
        if (br->ibuf_capacity > (SIZE_MAX >> 1)) br->ibuf_capacity = SIZE_MAX >> 1;
        /* bz2_br_refill never reads more than remains of a sized stream. */
        if (remaining >= 0) br->ibuf_capacity = xx_io_buffer_size_for(br->ibuf_capacity, (uint64_t)remaining);
        br->ibuf = (uint8_t *)xx_mem_alloc(br->ibuf_capacity);
        br->error = br->ibuf == NULL;
        if (br->error) xx_io_note_alloc_failure();
    }
    return !br->error;
}

void bz2_br_free(bz2_bit_reader *br)
{
    if (br && br->dev && br->ibuf) {
        xx_mem_free(br->ibuf);
        br->ibuf = NULL;
    }
}

static inline void bz2_ensure_bits(bz2_bit_reader *br, int n)
{
    while (br->n_bits < n) {
        if (br->ibuf_pos >= br->ibuf_len) {
            if (!bz2_br_refill(br)) return;
        }
        if (br->ibuf_len - br->ibuf_pos >= 4 && br->n_bits <= 32) {
            uint32_t w = xx_data_get_be32(br->ibuf + br->ibuf_pos);
            br->bits = (br->bits << 32) | (uint64_t)w;
            br->ibuf_pos += 4;
            br->n_bits += 32;
        } else {
            br->bits = (br->bits << 8) | (uint64_t)br->ibuf[br->ibuf_pos++];
            br->n_bits += 8;
        }
    }
}

static inline uint32_t bz2_read_bits(bz2_bit_reader *br, int n)
{
    if (br->n_bits < n) {
        bz2_ensure_bits(br, n);
        if (br->n_bits < n) {
            br->error = true;
            return 0;
        }
    }
    br->n_bits -= n;
    uint32_t mask = (n == 32) ? 0xFFFFFFFFu : (uint32_t)((1u << n) - 1u);
    return (uint32_t)(br->bits >> br->n_bits) & mask;
}

static inline uint32_t bz2_read_bit(bz2_bit_reader *br)
{
    return bz2_read_bits(br, 1);
}

/* =========================================================================
 * Bit-writer helpers
 * ========================================================================= */

bool bz2_bw_init(bz2_bit_writer *bw, xx_io_device *dev, uint8_t *mem, size_t mem_cap)
{
    xx_rt_memset(bw, 0, sizeof(*bw));
    bw->dev = dev;
    bw->mem = mem;
    bw->mem_cap = mem_cap;
    bw->obuf_capacity = xx_get_file_buffer_size();
    if (bw->obuf_capacity > (SIZE_MAX >> 1)) bw->obuf_capacity = SIZE_MAX >> 1;
    bw->obuf = (uint8_t *)xx_mem_alloc(bw->obuf_capacity);
    bw->error = bw->obuf == NULL;
    return !bw->error;
}

static bool bz2_bw_flush_buffer(bz2_bit_writer *bw)
{
    if (bw->obuf_pos > 0) {
        if (bw->dev) {
            ssize_t w = xx_io_write(bw->dev, bw->obuf, bw->obuf_pos);
            if (w < 0 || (size_t)w != bw->obuf_pos) {
                bw->error = true;
                return false;
            }
        } else if (bw->mem) {
            if (bw->mem_pos + bw->obuf_pos > bw->mem_cap) {
                bw->error = true;
                return false;
            }
            xx_rt_memcpy(bw->mem + bw->mem_pos, bw->obuf, bw->obuf_pos);
            bw->mem_pos += bw->obuf_pos;
        }
        bw->total_written += (int64_t)bw->obuf_pos;
        bw->obuf_pos = 0;
    }
    return !bw->error;
}

static bool bz2_bw_write_byte(bz2_bit_writer *bw, uint8_t b)
{
    if (bw->error) return false;
    if (bw->obuf_pos >= bw->obuf_capacity) {
        if (!bz2_bw_flush_buffer(bw)) return false;
    }
    bw->obuf[bw->obuf_pos++] = b;
    return true;
}

bool bz2_bw_write_bits(bz2_bit_writer *bw, uint32_t val, int n)
{
    uint32_t mask = (n == 32) ? 0xFFFFFFFFu : (uint32_t)((1u << n) - 1u);
    bw->bits = (bw->bits << n) | (uint64_t)(val & mask);
    bw->n_bits += n;
    while (bw->n_bits >= 8) {
        bw->n_bits -= 8;
        if (!bz2_bw_write_byte(bw, (uint8_t)(bw->bits >> bw->n_bits))) return false;
    }
    return true;
}

bool bz2_bw_flush(bz2_bit_writer *bw)
{
    /* Pad remaining bits */
    if (bw->n_bits > 0) {
        uint8_t b = (uint8_t)(bw->bits << (8 - bw->n_bits));
        bw->n_bits = 0;
        bw->bits = 0;
        if (!bz2_bw_write_byte(bw, b)) return false;
    }
    return bz2_bw_flush_buffer(bw);
}

void bz2_bw_free(bz2_bit_writer *bw)
{
    if (bw) {
        xx_mem_free(bw->obuf);
        bw->obuf = NULL;
    }
}


bool bz2_br_skip_bits(bz2_bit_reader *br, int n)
{
    while (n > 0) {
        int step = n > 24 ? 24 : n;
        (void)bz2_read_bits(br, step);
        if (br->error) return false;
        n -= step;
    }
    return true;
}

/* =========================================================================
 * Platform helpers
 * ========================================================================= */

/* XX_BZIP2_NO_SIMD=1 builds the portable code only (used to test it on x86). */
#if defined(XX_BZIP2_NO_SIMD) && XX_BZIP2_NO_SIMD
#define BZ2_X86_SIMD 0
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define BZ2_X86_SIMD 1
#define BZ2_TARGET_SSE2
#define BZ2_TARGET_SSSE3
#include <emmintrin.h>
#include <tmmintrin.h>
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#define BZ2_X86_SIMD 1
#define BZ2_TARGET_SSE2 __attribute__((target("sse2")))
#define BZ2_TARGET_SSSE3 __attribute__((target("ssse3")))
#include <emmintrin.h>
#include <tmmintrin.h>
#else
#define BZ2_X86_SIMD 0
#endif

#if defined(_MSC_VER)
#define BZ2_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define BZ2_NOINLINE __attribute__((noinline))
#else
#define BZ2_NOINLINE
#endif

/* Input bytes a fast group decode may touch: 50 codes of at most 20 bits,
 * the bit buffer's read-ahead and one 8-byte load. */
#define BZ2_FAST_GROUP_INPUT 160

/* Independent pointer chains that rebuild a block (see bz2_block_ibwt). */
#define BZ2_IBWT_CHAINS 8

/* =========================================================================
 * Huffman decoder
 * ========================================================================= */

static bool bz2_huff_build(bz2_huffman *ht, const uint8_t *lengths, int n_syms)
{
    int count[BZ2_MAX_CODE_LEN + 1] = {0};
    int first_index[BZ2_MAX_CODE_LEN + 1] = {0};
    int first_code[BZ2_MAX_CODE_LEN + 1] = {0};
    int idx[BZ2_MAX_CODE_LEN + 1];
    int total = 0;
    int code = 0;
    if (!ht || !lengths || n_syms < 1 || n_syms > BZ2_MAX_ALPHA_SIZE) {
        return false;
    }
    for (int i = 0; i < n_syms; i++) {
        if (lengths[i] < 1 || lengths[i] > BZ2_MAX_CODE_LEN) {
            return false;
        }
        count[(int)lengths[i]]++;
    }
    ht->max_len = 0;
    for (int len = 1; len <= BZ2_MAX_CODE_LEN; len++) {
        first_index[len] = total;
        total += count[len];
        if (count[len] > 0) ht->max_len = len;
    }

    ht->upper[0] = 0;
    ht->offset[0] = 0;
    for (int len = 1; len <= BZ2_MAX_CODE_LEN; len++) {
        code = (code + count[len - 1]) << 1;
        if (code > (1 << len) || count[len] > (1 << len) - code) {
            return false;
        }
        first_code[len] = code;
        ht->upper[len] = (uint32_t)(code + count[len]) << (BZ2_MAX_CODE_LEN - len);
        ht->offset[len] = first_index[len] - code;
    }
    ht->upper[BZ2_MAX_CODE_LEN + 1] = 0xFFFFFFFFu;

    /* Sort symbols by length into ht->perm */
    for (int len = 0; len <= BZ2_MAX_CODE_LEN; len++) idx[len] = first_index[len];
    for (int i = 0; i < n_syms; i++) ht->perm[idx[lengths[i]]++] = (uint16_t)i;

    /* Lookup table of the codes of at most BZ2_HUFF_FAST_BITS bits */
    for (int i = 0; i < BZ2_HUFF_FAST_SIZE; i++) ht->fast[i] = 0;
    for (int len = 1; len <= BZ2_HUFF_FAST_BITS; len++) {
        int shift = BZ2_HUFF_FAST_BITS - len;
        int num_pads = 1 << shift;
        for (int c = first_code[len]; c < first_code[len] + count[len]; c++) {
            uint16_t entry = (uint16_t)(((unsigned)ht->perm[c + ht->offset[len]] << 5) | (unsigned)len);
            uint16_t *slot = ht->fast + ((size_t)c << shift);
            for (int pad = 0; pad < num_pads; pad++) slot[pad] = entry;
        }
    }
    return true;
}

/* A code longer than BZ2_HUFF_FAST_BITS from the next 20 stream bits.
 * Returns its length, 0 if no code matches. */
static inline unsigned bz2_huff_long(const bz2_huffman *ht, uint32_t code20, unsigned *sym)
{
    unsigned len = BZ2_HUFF_FAST_BITS + 1;
    unsigned max_len = (unsigned)ht->max_len;
    while (len <= max_len && code20 >= ht->upper[len]) len++;
    if (len > max_len) return 0;
    *sym = ht->perm[(int32_t)(code20 >> (BZ2_MAX_CODE_LEN - len)) + ht->offset[len]];
    return len;
}

/* One symbol with input checks: used near the end of the input buffer. */
static int bz2_huff_decode_checked(bz2_bit_reader *br, const bz2_huffman *ht)
{
    uint32_t code20;
    unsigned len;
    unsigned sym = 0;
    unsigned entry;
    if (br->n_bits < BZ2_MAX_CODE_LEN) bz2_ensure_bits(br, BZ2_MAX_CODE_LEN);
    if (br->n_bits >= BZ2_MAX_CODE_LEN) {
        code20 = (uint32_t)(br->bits >> (br->n_bits - BZ2_MAX_CODE_LEN)) & 0xFFFFFu;
    } else if (br->n_bits > 0) {
        code20 = (uint32_t)(br->bits << (BZ2_MAX_CODE_LEN - br->n_bits)) & 0xFFFFFu;
    } else {
        br->error = true;
        return -1;
    }
    entry = ht->fast[code20 >> (BZ2_MAX_CODE_LEN - BZ2_HUFF_FAST_BITS)];
    if (entry) {
        len = entry & 31u;
        sym = entry >> 5;
    } else {
        len = bz2_huff_long(ht, code20, &sym);
        if (len == 0) return -1; /* bad stream */
    }
    if ((int)len > br->n_bits) {
        /* The code runs past the end of the input. */
        br->error = true;
        return -1;
    }
    br->n_bits -= (int)len;
    return (int)sym;
}

/* Up to 50 symbols of one group, without input checks: the caller makes
 * sure BZ2_FAST_GROUP_INPUT bytes are readable at *pp. Stops after the
 * end-of-block symbol. Returns the number of symbols, -1 on a bad code. */
static inline int bz2_huff_group_fast(const uint8_t **pp, uint64_t *pbb, unsigned *pbc, const bz2_huffman *ht, uint16_t *syms, unsigned eob)
{
    const uint8_t *p = *pp;
    uint64_t bb = *pbb;
    unsigned bc = *pbc;
    int i = 0;
    while (i < BZ2_GROUP_SIZE) {
        unsigned entry, len, sym;
        bb |= xx_data_get_be64(p) >> bc;
        p += (63u - bc) >> 3;
        bc |= 56u;
        entry = ht->fast[bb >> (64 - BZ2_HUFF_FAST_BITS)];
        if (entry) {
            len = entry & 31u;
            sym = entry >> 5;
        } else {
            sym = 0;
            len = bz2_huff_long(ht, (uint32_t)(bb >> (64 - BZ2_MAX_CODE_LEN)), &sym);
            if (len == 0) return -1;
        }
        bb <<= len;
        bc -= len;
        syms[i++] = (uint16_t)sym;
        if (sym == eob) break;
    }
    *pp = p;
    *pbb = bb;
    *pbc = bc;
    return i;
}

/* =========================================================================
 * Move-to-front and zero-run stage
 *
 * The decoded block is stored as (rank << 8) | byte, where rank counts the
 * earlier occurrences of the byte: adding the number of smaller bytes gives
 * the LF mapping of the inverse BWT.
 * ========================================================================= */

/* Returns 0: group done, 1: end of block, -1: invalid data. */
static BZ2_NOINLINE int bz2_mtf_stage_scalar(bz2_block_work *w, const uint16_t *syms, int count)
{
    uint8_t *m = w->mtf;
    uint32_t *tt = w->tt;
    uint32_t *freq = w->freq;
    uint32_t n = w->n;
    uint32_t run = w->run_count;
    uint32_t weight = w->run_weight;
    const uint32_t limit = w->limit;
    const uint32_t half = limit / 2U;
    const unsigned eob = w->eob;
    int rc = 0;
    for (int i = 0; i < count; i++) {
        unsigned s = syms[i];
        uint32_t b, f;
        unsigned idx;
        if (s < 2U) {
            /* bijective base-2: RUNA adds the weight, RUNB twice the weight */
            uint32_t contribution = weight << s;
            uint32_t available = limit - n;
            if (contribution > available || run > available - contribution) {
                rc = -1;
                break;
            }
            run += contribution;
            weight = weight > half ? limit + 1U : weight << 1;
            continue;
        }
        if (run) {
            b = m[0];
            f = freq[b];
            freq[b] = f + run;
            for (uint32_t k = 0; k < run; k++) tt[n + k] = ((f + k) << 8) | b;
            n += run;
            run = 0;
            weight = 1;
        }
        if (s == eob) {
            rc = 1;
            break;
        }
        if (n >= limit) {
            rc = -1;
            break;
        }
        idx = s - 1U;
        b = m[idx];
        for (unsigned k = idx; k > 0; k--) m[k] = m[k - 1];
        m[0] = (uint8_t)b;
        f = freq[b];
        freq[b] = f + 1U;
        tt[n++] = (f << 8) | b;
    }
    w->n = n;
    w->run_count = run;
    w->run_weight = weight;
    return rc;
}

#if BZ2_X86_SIMD
/* One group of up to 50 symbols, Huffman decoding and move-to-front in one
 * loop, so that the two dependency chains overlap. No input checks: the
 * caller makes sure BZ2_FAST_GROUP_INPUT bytes are readable at *pp.
 * The first 16 list entries live in a register and move by one byte
 * shuffle; the other rows are shifted 16 bytes at a time.
 * Returns 0: group done, 1: end of block, -1: invalid data. */
BZ2_TARGET_SSSE3 static BZ2_NOINLINE int bz2_group_fast_ssse3(bz2_block_work *w, const uint8_t **pp, uint64_t *pbb, unsigned *pbc, const bz2_huffman *ht)
{
    uint8_t *m = w->mtf;
    uint32_t *tt = w->tt;
    uint32_t *freq = w->freq;
    uint32_t n = w->n;
    uint32_t run = w->run_count;
    uint32_t weight = w->run_weight;
    const uint32_t limit = w->limit;
    const uint32_t half = limit / 2U;
    const unsigned eob = w->eob;
    const uint8_t *p = *pp;
    uint64_t bb = *pbb;
    unsigned bc = *pbc;
    __m128i front = _mm_loadu_si128((const __m128i *)m);
    int rc = 0;
    for (int i = 0; i < BZ2_GROUP_SIZE; i++) {
        unsigned entry, len, s;
        uint32_t b, f;
        unsigned idx;

        bb |= xx_data_get_be64(p) >> bc;
        p += (63u - bc) >> 3;
        bc |= 56u;
        entry = ht->fast[bb >> (64 - BZ2_HUFF_FAST_BITS)];
        if (entry) {
            len = entry & 31u;
            s = entry >> 5;
        } else {
            s = 0;
            len = bz2_huff_long(ht, (uint32_t)(bb >> (64 - BZ2_MAX_CODE_LEN)), &s);
            if (len == 0) {
                rc = -1;
                break;
            }
        }
        bb <<= len;
        bc -= len;

        if (s < 2U) {
            /* bijective base-2: RUNA adds the weight, RUNB twice the weight */
            uint32_t contribution = weight << s;
            uint32_t available = limit - n;
            if (contribution > available || run > available - contribution) {
                rc = -1;
                break;
            }
            run += contribution;
            weight = weight > half ? limit + 1U : weight << 1;
            continue;
        }
        if (run) {
            __m128i v;
            uint32_t *d = tt + n;
            uint32_t k = 0;
            b = (uint32_t)_mm_cvtsi128_si32(front) & 0xFFu;
            f = freq[b];
            freq[b] = f + run;
            v = _mm_add_epi32(_mm_set1_epi32((int)((f << 8) | b)), _mm_setr_epi32(0, 256, 512, 768));
            do {
                _mm_storeu_si128((__m128i *)(d + k), v);
                v = _mm_add_epi32(v, _mm_set1_epi32(1024));
                k += 4;
            } while (k < run);
            n += run;
            run = 0;
            weight = 1;
        }
        if (s == eob) {
            rc = 1;
            break;
        }
        if (n >= limit) {
            rc = -1;
            break;
        }
        idx = s - 1U;
        if (idx < 16U) {
            front = _mm_shuffle_epi8(front, _mm_loadu_si128((const __m128i *)w->shuf_front[idx]));
            b = (uint32_t)_mm_cvtsi128_si32(front) & 0xFFu;
        } else {
            /* Rows 1..q-1 move up by one byte: an unaligned copy from one
             * byte lower. Row q does so up to position r only. Row 0 is the
             * register, whose last byte enters row 1. All loads of a row
             * precede the store into the row below it. */
            unsigned q = idx >> 4;
            unsigned r = idx & 15U;
            const __m128i low = _mm_loadu_si128((const __m128i *)w->mask_low[r]);
            const __m128i first = _mm_loadu_si128((const __m128i *)w->mask_first[q == 1U]);
            const __m128i carry = _mm_srli_si128(front, 15);
            const __m128i row1 = _mm_loadu_si128((const __m128i *)(m + 16));
            const __m128i rowq = _mm_loadu_si128((const __m128i *)(m + 16U * q));
            __m128i shifted = _mm_loadu_si128((const __m128i *)(m + 16U * q - 1U));
            b = m[idx];
            /* For q == 1 the byte below row q is the register's, not memory's. */
            shifted = _mm_or_si128(_mm_andnot_si128(first, shifted), _mm_and_si128(first, carry));
            for (unsigned k = q - 1U; k >= 2U; k--) {
                _mm_storeu_si128((__m128i *)(m + 16U * k), _mm_loadu_si128((const __m128i *)(m + 16U * k - 1U)));
            }
            _mm_storeu_si128((__m128i *)(m + 16), _mm_or_si128(_mm_slli_si128(row1, 1), carry));
            _mm_storeu_si128((__m128i *)(m + 16U * q), _mm_or_si128(_mm_and_si128(low, shifted), _mm_andnot_si128(low, rowq)));
            front = _mm_or_si128(_mm_slli_si128(front, 1), _mm_cvtsi32_si128((int)b));
        }
        f = freq[b];
        freq[b] = f + 1U;
        tt[n++] = (f << 8) | b;
    }
    _mm_storeu_si128((__m128i *)m, front);
    w->n = n;
    w->run_count = run;
    w->run_weight = weight;
    *pp = p;
    *pbb = bb;
    *pbc = bc;
    return rc;
}
#endif

/* =========================================================================
 * Scratch memory
 * ========================================================================= */

bool bz2_work_init(bz2_block_work *w, int block_bytes)
{
    if (!w) return false;
    w->tt = NULL;
    w->tt2 = NULL;
    w->mid = NULL;
    w->block_cap = 0;
    if (block_bytes < 100000 || block_bytes > BZ2_MAX_BLOCK_SIZE) return false;
    w->tt = (uint32_t *)xx_mem_alloc(((size_t)block_bytes + BZ2_WORK_PAD) * sizeof(uint32_t));
    w->tt2 = (uint32_t *)xx_mem_alloc(((size_t)block_bytes + BZ2_WORK_PAD) * sizeof(uint32_t));
    w->mid = (uint8_t *)xx_mem_alloc((size_t)block_bytes + BZ2_WORK_PAD);
    if (!w->tt || !w->tt2 || !w->mid) {
        bz2_work_free(w);
        return false;
    }
    /* The slack is read, never used: keep it defined.  The inverse BWT
     * prefetches through tt / tt2 entries past n, so their slack must hold
     * in-range indices from the first block on (0 is one). */
    xx_rt_memset(w->mid + block_bytes, 0, BZ2_WORK_PAD);
    xx_rt_memset(w->tt + block_bytes, 0, BZ2_WORK_PAD * sizeof(uint32_t));
    xx_rt_memset(w->tt2 + block_bytes, 0, BZ2_WORK_PAD * sizeof(uint32_t));
    w->block_cap = block_bytes;
#if BZ2_X86_SIMD
    /* SSE4.2 is the level the library detects; it includes SSSE3. */
    w->simd = xx_is_sse2_enabled() && xx_is_sse42_enabled();
#else
    w->simd = false;
#endif
    for (int r = 0; r < 16; r++) {
        for (int k = 0; k < 16; k++) {
            uint8_t from = (uint8_t)(k == 0 ? r : (k <= r ? k - 1 : k));
            w->shuf_front[r][k] = from;
            w->mask_low[r][k] = (uint8_t)(k <= r ? 0xFF : 0);
        }
    }
    for (int k = 0; k < 16; k++) {
        w->mask_first[0][k] = 0;
        w->mask_first[1][k] = (uint8_t)(k == 0 ? 0xFF : 0);
    }
    return true;
}

void bz2_work_free(bz2_block_work *w)
{
    if (!w) return;
    if (w->tt) xx_mem_free(w->tt);
    if (w->tt2) xx_mem_free(w->tt2);
    if (w->mid) xx_mem_free(w->mid);
    w->tt = NULL;
    w->tt2 = NULL;
    w->mid = NULL;
    w->block_cap = 0;
}

/* =========================================================================
 * One block: header, Huffman, move-to-front, zero runs
 * ========================================================================= */

bool bz2_block_read(bz2_bit_reader *br, bz2_block_work *w, int block_bytes, bz2_block_info *info)
{
    if (!br || !w || !info || block_bytes < 100000 || block_bytes > BZ2_MAX_BLOCK_SIZE || block_bytes > w->block_cap) return false;

    /* Block header: 6-byte magic already consumed by caller */
    uint32_t block_crc = bz2_read_bits(br, 32);
    int rand_flag = (int)bz2_read_bit(br);
    uint32_t orig_ptr = bz2_read_bits(br, 24);
    if (br->error) return false;

    /* Symbol map: which bytes appear in this block */
    uint16_t in_use_16 = (uint16_t)bz2_read_bits(br, 16);
    uint16_t sym_map[16];
    for (int hi = 0; hi < 16; hi++) {
        if (in_use_16 & (1u << (15 - hi))) {
            sym_map[hi] = (uint16_t)bz2_read_bits(br, 16);
        } else {
            sym_map[hi] = 0;
        }
    }
    if (br->error) return false;

    int n_syms = 0;
    xx_rt_memset(w->mtf, 0, sizeof(w->mtf));
    for (int hi = 0; hi < 16; hi++) {
        if (sym_map[hi]) {
            for (int lo = 0; lo < 16; lo++) {
                if (sym_map[hi] & (1u << (15 - lo))) {
                    /* The move-to-front list holds the bytes themselves. */
                    w->mtf[n_syms++] = (uint8_t)(hi * 16 + lo);
                }
            }
        }
    }
    /* +2 for RUNA, RUNB */
    if (n_syms == 0) return false;
    int alpha_size = n_syms + 2;

    int n_groups = (int)bz2_read_bits(br, 3);
    int n_selectors = (int)bz2_read_bits(br, 15);
    if (br->error || n_groups < 1 || n_groups > BZ2_N_GROUPS || n_selectors < 1) return false;

    /* Read selectors (MTF-coded) */
    uint8_t sel_list[BZ2_N_GROUPS];
    for (int i = 0; i < n_groups; i++) sel_list[i] = (uint8_t)i;
    for (int i = 0; i < n_selectors; i++) {
        int run = 0;
        while (bz2_read_bit(br)) {
            run++;
            if (run >= n_groups) return false;
        }
        if (br->error) return false;
        uint8_t tmp = sel_list[run];
        for (int k = run; k > 0; k--) sel_list[k] = sel_list[k - 1];
        sel_list[0] = tmp;
        w->sel[i] = tmp;
    }

    /* Read Huffman code lengths and build tables */
    for (int g = 0; g < n_groups; g++) {
        uint8_t lengths[BZ2_MAX_ALPHA_SIZE];
        int curr = (int)bz2_read_bits(br, 5);
        if (br->error) return false;
        for (int i = 0; i < alpha_size; i++) {
            while (bz2_read_bit(br)) {
                if (br->error) return false;
                curr += bz2_read_bit(br) ? -1 : 1;
                if (br->error || curr < 1 || curr > BZ2_MAX_CODE_LEN) return false;
            }
            lengths[i] = (uint8_t)curr;
        }
        if (br->error) return false;
        if (!bz2_huff_build(&w->htabs[g], lengths, alpha_size)) return false;
    }

    xx_rt_memset(w->freq, 0, sizeof(w->freq));
    w->n = 0;
    w->limit = (uint32_t)block_bytes;
    w->run_count = 0;
    w->run_weight = 1; /* bit-position weight: 1, 2, 4, 8, ... */
    w->eob = (unsigned)(alpha_size - 1);

    int sel_idx = 0;
    for (;;) {
        uint16_t syms[BZ2_GROUP_SIZE];
        const bz2_huffman *ht;
        int count;
        int rc;
        if (sel_idx >= n_selectors) return false;
        ht = &w->htabs[w->sel[sel_idx++]];

        if (br->ibuf_len - br->ibuf_pos >= BZ2_FAST_GROUP_INPUT && br->n_bits >= 0 && br->n_bits <= 63) {
            const uint8_t *p = br->ibuf + br->ibuf_pos;
            unsigned bc = (unsigned)br->n_bits;
            uint64_t bb = bc ? br->bits << (64u - bc) : 0;
#if BZ2_X86_SIMD
            if (w->simd) {
                rc = bz2_group_fast_ssse3(w, &p, &bb, &bc, ht);
                if (rc < 0) return false;
                count = -1;
            } else
#endif
            {
                count = bz2_huff_group_fast(&p, &bb, &bc, ht, syms, w->eob);
                if (count < 0) return false;
                rc = 0;
            }
            br->ibuf_pos = (size_t)(p - br->ibuf);
            br->n_bits = (int)bc;
            br->bits = bc ? bb >> (64u - bc) : 0;
        } else {
            /* Near the end of the input buffer: one checked symbol at a time. */
            count = 0;
            rc = 0;
            while (count < BZ2_GROUP_SIZE) {
                int sym = bz2_huff_decode_checked(br, ht);
                if (br->error || sym < 0) return false;
                syms[count++] = (uint16_t)sym;
                if ((unsigned)sym == w->eob) break;
            }
        }
        if (count >= 0) rc = bz2_mtf_stage_scalar(w, syms, count);
        if (rc < 0) return false;
        if (rc > 0) break;
    }

    if (w->n == 0 || orig_ptr >= w->n) return false;
    info->n = w->n;
    info->orig_ptr = orig_ptr;
    info->stored_crc = block_crc;
    info->randomised = rand_flag != 0;
    return true;
}

bool bz2_block_read_mem(const uint8_t *mem, size_t size, uint64_t bit_pos, bz2_block_work *w, int block_bytes, bz2_block_info *info, uint64_t *end_bit)
{
    bz2_bit_reader br;
    size_t skip = (size_t)(bit_pos >> 3);
    uint32_t hi;
    uint16_t lo;
    if (!mem || !end_bit || (bit_pos >> 3) >= (uint64_t)size) return false;
    if (!bz2_br_init(&br, NULL, mem + skip, size - skip, -1)) return false;
    if (!bz2_br_skip_bits(&br, (int)(bit_pos & 7u))) return false;
    hi = bz2_read_bits(&br, 32);
    lo = (uint16_t)bz2_read_bits(&br, 16);
    if (br.error || hi != 0x31415926UL || lo != 0x5359U) return false;
    if (!bz2_block_read(&br, w, block_bytes, info)) return false;
    *end_bit = ((uint64_t)skip + (uint64_t)br.ibuf_pos) * 8u - (uint64_t)br.n_bits;
    return true;
}

/* =========================================================================
 * Inverse BWT
 * ========================================================================= */

/* The reference traversal: the forward successor table, followed from
 * orig_ptr. Used when the fast method cannot be (see bz2_block_ibwt). */
static BZ2_NOINLINE void bz2_ibwt_reference(const uint32_t *lf, uint32_t *succ, uint8_t *mid, uint32_t n, uint32_t orig_ptr, const uint32_t *freq)
{
    uint32_t cum[256];
    uint32_t sum = 0;
    uint32_t t;
    for (int i = 0; i < 256; i++) {
        cum[i] = sum;
        sum += freq[i];
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t b = lf[i] & 0xFFu;
        succ[cum[b]++] = (i << 8) | b;
    }
    t = succ[orig_ptr];
    for (uint32_t i = 0; i < n; i++) {
        mid[i] = (uint8_t)t;
        t = succ[t >> 8];
    }
}

/* dst[i] = src[src[i]]: the mapping composed with itself; the byte of
 * entry i is kept. The reads are random: they are prefetched well ahead
 * and the result bypasses the cache. src has BZ2_WORK_PAD entries of
 * slack, which hold indices below n. */
#define BZ2_IBWT_PREFETCH 32
#if BZ2_X86_SIMD
BZ2_TARGET_SSE2 static BZ2_NOINLINE void bz2_ibwt_double_sse2(uint32_t *dst, const uint32_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t x = src[i];
        _mm_prefetch((const char *)&src[src[i + BZ2_IBWT_PREFETCH] >> 8], _MM_HINT_T0);
        _mm_stream_si32((int *)&dst[i], (int)((src[x >> 8] & 0xFFFFFF00u) | (x & 0xFFu)));
    }
    _mm_sfence();
}
#endif

static void bz2_ibwt_double(uint32_t *dst, const uint32_t *src, uint32_t n, bool simd)
{
#if BZ2_X86_SIMD
    if (simd) {
        bz2_ibwt_double_sse2(dst, src, n);
        return;
    }
#else
    (void)simd;
#endif
    for (uint32_t i = 0; i < n; i++) {
        uint32_t x = src[i];
        dst[i] = (src[x >> 8] & 0xFFFFFF00u) | (x & 0xFFu);
    }
}

/* The block is rebuilt backwards with the LF mapping (row -> row of the
 * preceding byte). LF is composed with itself three times, so that eight
 * independent pointer chains walk LF^8 and overlap their cache misses:
 * chain c starts at LF^c(orig_ptr) and yields the bytes n-1-c, n-1-c-8, ... */
void bz2_block_ibwt(bz2_block_work *w, const bz2_block_info *info)
{
    uint32_t cum[256];
    uint32_t row[BZ2_IBWT_CHAINS];
    uint32_t *a = w->tt;
    uint32_t *b = w->tt2;
    uint8_t *mid = w->mid;
    const uint32_t n = info->n;
    const uint32_t full = n / BZ2_IBWT_CHAINS;
    const uint32_t rest = n % BZ2_IBWT_CHAINS;
    uint32_t sum = 0;

    for (int i = 0; i < 256; i++) {
        cum[i] = sum;
        sum += w->freq[i];
    }

    /* LF^c(orig_ptr): where the chains start. */
    row[0] = info->orig_ptr;
    for (int c = 1; c < BZ2_IBWT_CHAINS; c++) {
        uint32_t x = a[row[c - 1]];
        row[c] = (x >> 8) + cum[x & 0xFFu];
    }

    /* Round 1: ranks to LF, composed with itself (tt2 = LF^2). The reads
     * follow one ascending sequence per byte value. */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t x = a[i];
        uint32_t y = a[(x >> 8) + cum[x & 0xFFu]];
        b[i] = (((y >> 8) + cum[y & 0xFFu]) << 8) | (x & 0xFFu);
    }
    bz2_ibwt_double(a, b, n, w->simd); /* tt = LF^4 */
    bz2_ibwt_double(b, a, n, w->simd); /* tt2 = LF^8 */

    {
        uint8_t *p = mid + n;
        uint32_t r0 = row[0], r1 = row[1], r2 = row[2], r3 = row[3];
        uint32_t r4 = row[4], r5 = row[5], r6 = row[6], r7 = row[7];
#define BZ2_CHAIN_STEP(r, at) \
    do { \
        uint32_t x_ = b[r]; \
        p[at] = (uint8_t)x_; \
        r = x_ >> 8; \
    } while (0)
        for (uint32_t k = 0; k < full; k++) {
            p -= 8;
            BZ2_CHAIN_STEP(r0, 7);
            BZ2_CHAIN_STEP(r1, 6);
            BZ2_CHAIN_STEP(r2, 5);
            BZ2_CHAIN_STEP(r3, 4);
            BZ2_CHAIN_STEP(r4, 3);
            BZ2_CHAIN_STEP(r5, 2);
            BZ2_CHAIN_STEP(r6, 1);
            BZ2_CHAIN_STEP(r7, 0);
        }
#undef BZ2_CHAIN_STEP
        row[0] = r0;
        row[1] = r1;
        row[2] = r2;
        row[3] = r3;
        row[4] = r4;
        row[5] = r5;
        row[6] = r6;
        row[7] = r7;
    }
    /* The first bytes: one more step of the chains 0..rest-1. */
    for (uint32_t c = 0; c < rest; c++) {
        uint32_t x = b[row[c]];
        mid[rest - 1U - c] = (uint8_t)x;
        row[c] = x >> 8;
    }

    /* The chains walk LF backwards from orig_ptr, which gives the block
     * when n steps of LF return to orig_ptr (always so for a real BWT).
     * Chain `rest` has made exactly n steps. Data that fails this
     * (damaged, or crafted with several cycles) takes the reference path,
     * whose output defines the format. */
    if (row[rest] != info->orig_ptr) {
        bz2_ibwt_reference(b, a, mid, n, info->orig_ptr, w->freq);
    }

    if (info->randomised) {
        unsigned random_left = 0U;
        unsigned random_index = 0U;
        for (uint32_t i = 0; i < n; i++) {
            if (random_left == 0U) {
                random_left = bz2_random_numbers[random_index];
                random_index = (random_index + 1U) & 511U;
            }
            --random_left;
            if (random_left == 1U) mid[i] ^= 1U;
        }
    }
}

/* =========================================================================
 * Final run-length stage: 4 equal bytes are followed by a repeat count
 * ========================================================================= */

void bz2_rle1_init(bz2_rle1 *st)
{
    st->pos = 0;
    st->lit_end = 0;
    st->pend = 0;
    st->pend_byte = 0;
    st->has_run = false;
}

bool bz2_rle1_done(const bz2_rle1 *st, size_t n)
{
    return st->pos >= n && st->pend == 0 && !st->has_run;
}

#if BZ2_X86_SIMD
BZ2_TARGET_SSE2 static size_t bz2_rle1_find_sse2(const uint8_t *mid, size_t pos, size_t n)
{
    /* Loads run up to 18 bytes past n - 4: inside the slack after mid. */
    while (pos + 4 <= n) {
        __m128i x = _mm_loadu_si128((const __m128i *)(mid + pos));
        __m128i e = _mm_cmpeq_epi8(x, _mm_loadu_si128((const __m128i *)(mid + pos + 1)));
        e = _mm_and_si128(e, _mm_cmpeq_epi8(x, _mm_loadu_si128((const __m128i *)(mid + pos + 2))));
        e = _mm_and_si128(e, _mm_cmpeq_epi8(x, _mm_loadu_si128((const __m128i *)(mid + pos + 3))));
        unsigned mask = (unsigned)_mm_movemask_epi8(e);
        if (mask) {
            unsigned bit = 0;
            while (!(mask & 1u)) {
                mask >>= 1;
                bit++;
            }
            pos += bit;
            return pos + 4 <= n ? pos : n;
        }
        pos += 16;
    }
    return n;
}
#endif

/* First s >= pos with mid[s..s+3] equal and s + 4 <= n, else n. */
static size_t bz2_rle1_find(const uint8_t *mid, size_t pos, size_t n, bool simd)
{
#if BZ2_X86_SIMD
    if (simd) return bz2_rle1_find_sse2(mid, pos, n);
#else
    (void)simd;
#endif
    while (pos + 4 <= n) {
        if (mid[pos + 3] != mid[pos + 2]) pos += 3;
        else if (mid[pos + 2] != mid[pos + 1]) pos += 2;
        else if (mid[pos + 1] != mid[pos]) pos += 1;
        else return pos;
    }
    return n;
}

static size_t bz2_rle1_expand_ex(bz2_rle1 *st, const uint8_t *mid, size_t n, uint8_t *dst, size_t cap, bool simd)
{
    size_t done = 0;
    for (;;) {
        size_t chunk;
        if (st->pend) {
            chunk = st->pend < cap - done ? st->pend : cap - done;
            if (chunk) {
                xx_rt_memset(dst + done, st->pend_byte, chunk);
                done += chunk;
                st->pend -= (uint32_t)chunk;
            }
            if (st->pend) return done;
        }
        if (st->pos == st->lit_end && !st->has_run) {
            size_t s;
            if (st->pos >= n) return done;
            s = bz2_rle1_find(mid, st->pos, n, simd);
            if (s < n) {
                st->lit_end = s + 4;
                st->has_run = s + 4 < n; /* a count byte follows */
            } else {
                st->lit_end = n;
            }
        }
        chunk = st->lit_end - st->pos;
        if (chunk > cap - done) chunk = cap - done;
        if (chunk) {
            xx_rt_memcpy(dst + done, mid + st->pos, chunk);
            done += chunk;
            st->pos += chunk;
        }
        if (st->pos < st->lit_end) return done;
        if (st->has_run) {
            st->pend = mid[st->pos];
            st->pend_byte = mid[st->pos - 1];
            st->pos++;
            st->lit_end = st->pos;
            st->has_run = false;
        }
    }
}

size_t bz2_rle1_expand(bz2_rle1 *st, const uint8_t *mid, size_t n, uint8_t *dst, size_t cap)
{
#if BZ2_X86_SIMD
    return bz2_rle1_expand_ex(st, mid, n, dst, cap, xx_is_sse2_enabled());
#else
    return bz2_rle1_expand_ex(st, mid, n, dst, cap, false);
#endif
}

/* =========================================================================
 * Public decompression entry point
 * ========================================================================= */

typedef struct {
    xx_io_device *dev;
    uint8_t *mem;
    size_t mem_cap;
    size_t mem_pos;
    uint8_t *stage_buf;
    size_t stage_cap;
    bool error;
} bz2_out;

static bool bz2_write_all(xx_io_device *dev, const uint8_t *data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ssize_t w = xx_io_write(dev, data + done, size - done);
        if (w <= 0) return false;
        done += (size_t)w;
    }
    return true;
}

bool bz2_block_emit(xx_io_device *dev, const uint8_t *mid, uint32_t n, uint8_t **buf, size_t *cap, uint32_t *out_crc)
{
    bz2_rle1 rle;
    uint32_t crc = 0xFFFFFFFFU;
    size_t fill = 0;
    bz2_rle1_init(&rle);
    for (;;) {
        fill += bz2_rle1_expand(&rle, mid, n, *buf + fill, *cap - fill);
        if (bz2_rle1_done(&rle, n)) break;
        /* Long runs: the buffer doubles, as long as it is below 64 MiB
         * (a block is at most 46.6 MB). */
        if (*cap < ((size_t)64 << 20)) {
            uint8_t *grown = (uint8_t *)xx_mem_alloc(*cap * 2);
            if (grown) {
                xx_rt_memcpy(grown, *buf, fill);
                xx_mem_free(*buf);
                *buf = grown;
                *cap *= 2;
                continue;
            }
        }
        /* No larger buffer: the block goes out in parts. */
        crc = xx_crc32_mpeg2_raw(crc, *buf, fill);
        if (!bz2_write_all(dev, *buf, fill)) return false;
        fill = 0;
    }
    crc = xx_crc32_mpeg2_raw(crc, *buf, fill);
    *out_crc = crc ^ 0xFFFFFFFFU;
    return bz2_write_all(dev, *buf, fill);
}

/* One block with the sequential reader. The whole block is written before
 * its CRC is compared. */
static bool bz2_decompress_block(bz2_bit_reader *br, bz2_out *out, int block_bytes, bz2_block_work *w, uint32_t *calculated_crc)
{
    bz2_block_info info;
    bz2_rle1 rle;
    uint32_t crc = 0xFFFFFFFFU;

    if (!bz2_block_read(br, w, block_bytes, &info)) return false;
    bz2_block_ibwt(w, &info);
    bz2_rle1_init(&rle);

    if (out->dev) {
        if (!bz2_block_emit(out->dev, w->mid, info.n, &out->stage_buf, &out->stage_cap, &crc)) {
            out->error = true;
            return false;
        }
        crc ^= 0xFFFFFFFFU; /* back to the running form used below */
    } else if (!out->mem && out->stage_buf) {
        /* No destination: the output is only measured and checked. */
        do {
            size_t len = bz2_rle1_expand(&rle, w->mid, info.n, out->stage_buf, out->stage_cap);
            crc = xx_crc32_mpeg2_raw(crc, out->stage_buf, len);
            out->mem_pos += len;
        } while (!bz2_rle1_done(&rle, info.n));
    } else {
        size_t len = bz2_rle1_expand(&rle, w->mid, info.n, out->mem + out->mem_pos, out->mem_cap - out->mem_pos);
        if (!bz2_rle1_done(&rle, info.n)) {
            /* The block does not fit the destination. Nothing of it is
             * counted, except for a randomised block: the count then
             * includes what fits, but not a run that is cut short. */
            if (info.randomised) {
                if (rle.pend) len -= (size_t)w->mid[rle.pos - 1] - rle.pend;
                out->mem_pos += len;
            }
            out->error = true;
            return false;
        }
        crc = xx_crc32_mpeg2_raw(crc, out->mem + out->mem_pos, len);
        out->mem_pos += len;
    }

    crc ^= 0xFFFFFFFFU;
    if (info.stored_crc != crc) return false;
    *calculated_crc = crc;
    return true;
}

bool xx_bzip2_decompress_stream(bz2_bit_reader *br, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written, xx_pd_struct *pd)
{
    bz2_out out;
    bz2_block_work *work;
    xx_rt_memset(&out, 0, sizeof(out));
    out.dev = dst_dev;
    out.mem = dst_dev ? NULL : mem_dst;
    out.mem_cap = mem_cap;
    if (!br || br->error) return false;

    /* Stream header: "BZh" + digit (block-size) */
    uint8_t magic[4];
    magic[0] = (uint8_t)bz2_read_bits(br, 8);
    magic[1] = (uint8_t)bz2_read_bits(br, 8);
    magic[2] = (uint8_t)bz2_read_bits(br, 8);
    magic[3] = (uint8_t)bz2_read_bits(br, 8);
    if (br->error || magic[0] != 'B' || magic[1] != 'Z' || magic[2] != 'h' || magic[3] < '1' || magic[3] > '9') {
        return false;
    }
    /* The header limits the block after the first RLE stage, not the number
     * of bytes ultimately emitted. Reject oversized blocks before BWT inversion. */
    int block_bytes = (int)(magic[3] - '0') * 100000;

    /* Allocate scratch buffers once for the entire stream to avoid per-block heap allocation. */
    work = (bz2_block_work *)xx_mem_alloc(sizeof(*work));
    if (!work || !bz2_work_init(work, block_bytes)) {
        xx_io_note_alloc_failure();
        if (work) xx_mem_free(work);
        return false;
    }

    if (dst_dev || !mem_dst) {
        out.stage_cap = (size_t)block_bytes * 4;
        if (out.stage_cap < (1024 * 1024)) out.stage_cap = 1024 * 1024;
        out.stage_buf = (uint8_t *)xx_mem_alloc(out.stage_cap);
        if (!out.stage_buf) {
            xx_io_note_alloc_failure();
            bz2_work_free(work);
            xx_mem_free(work);
            return false;
        }
    }

#if XX_BZIP2_MT
    /* Blocks decoded one by one before (the next attempt at) the parallel
     * decoder: short streams never pay for threads. */
    int mt_threads = dst_dev ? bz2_mt_thread_count() : 0;
    uint64_t mt_wait = 2;
    uint64_t seq_blocks = 0;
#endif

    bool ok = true;
    bool saw_eos = false;
    uint32_t combined_crc = 0U;
    while (ok) {
        /* A block is at most 900 kB of output: a stop request is seen
         * between blocks. */
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
#if XX_BZIP2_MT
        if (mt_threads > 1 && seq_blocks >= mt_wait) {
            uint64_t done = 0;
            int rc = bz2_mt_run(br, dst_dev, block_bytes, &combined_crc, pd, mt_threads, &done);
            seq_blocks = 0;
            /* An attempt that decoded nothing makes the next one rarer. */
            if (done == 0 && mt_wait < ((uint64_t)1 << 40)) mt_wait *= 2;
            if (rc == BZ2_MT_FAILED) {
                out.error = true;
                ok = false;
                break;
            }
            if (br->error) {
                ok = false;
                break;
            }
        }
#endif
        /* Read 48-bit block magic (or EOS magic) */
        uint32_t hi = bz2_read_bits(br, 32);
        uint16_t lo = (uint16_t)bz2_read_bits(br, 16);
        if (br->error) {
            ok = false;
            break;
        }

        if (hi == 0x31415926UL && lo == 0x5359U) {
            /* data block */
            uint32_t block_crc = 0U;
            ok = bz2_decompress_block(br, &out, block_bytes, work, &block_crc);
            if (ok) {
                combined_crc = (combined_crc << 1) | (combined_crc >> 31);
                combined_crc ^= block_crc;
#if XX_BZIP2_MT
                seq_blocks++;
#endif
            }
        } else if (hi == 0x17724538UL && lo == 0x5090U) {
            uint32_t stored_crc = bz2_read_bits(br, 32);
            if (br->error || stored_crc != combined_crc) {
                ok = false;
            } else {
                saw_eos = true;
            }
            break;
        } else {
            ok = false;
        }
    }

    if (out_written) *out_written = out.mem_pos;
    if (out.stage_buf) xx_mem_free(out.stage_buf);
    bz2_work_free(work);
    xx_mem_free(work);
    return ok && saw_eos && !br->error && !out.error;
}
