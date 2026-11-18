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
#if defined(_MSC_VER)
#include <intrin.h>
#endif

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
        if (remaining_input >= 0 && (uint64_t)remaining_input <= sizeof(br->inline_buffer)) {
            br->buffer = br->inline_buffer;
            br->buffer_cap = sizeof(br->inline_buffer);
            br->owns_buffer = false;
        } else {
            size_t cap = 128 * 1024;
            if (remaining_input >= 0 && (uint64_t)remaining_input < (uint64_t)cap) {
                cap = (size_t)remaining_input;
                if (cap < sizeof(br->inline_buffer)) cap = sizeof(br->inline_buffer);
            }
            br->buffer = (uint8_t *)xx_mem_alloc(cap);
            if (!br->buffer) {
                br->buffer = br->inline_buffer;
                br->buffer_cap = sizeof(br->inline_buffer);
                br->owns_buffer = false;
            } else {
                br->buffer_cap = cap;
                br->owns_buffer = true;
            }
        }
    }
    return true;
}

void xx_br_free(xx_bit_reader *br)
{
    if (br->owns_buffer && br->buffer) {
        xx_mem_free(br->buffer);
    }
    br->buffer = NULL;
    br->owns_buffer = false;
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
    uint32_t v = (uint32_t)(br->bit_buf & ((1ULL << n) - 1));
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
/* --- Packed Huffman Tables                                             --- */
/* ========================================================================= */

/* One 32-bit entry per table slot (libdeflate-style layout):
 *   bits  0..7  bits the fast loop consumes for this entry (<= 31): the
 *               code length (plus the extra bits of a length/distance
 *               code); the main table bits for a sub-table link. In a
 *               sub-table the code length counts only the bits past the
 *               main table. Nothing else lives in this byte, so the fast
 *               loop may subtract whole entries from its bit count.
 *   bits  8..12 code length (as above, without extra bits), or the index
 *               bits of the linked sub-table
 *   bit  13     end of block, bit 14 sub-table link, bit 15 EXCEPTIONAL
 *               (end of block, sub-table link, or BAD when alone)
 *   bits 16..31 literal byte, length/distance base or sub-table offset
 *   bit  30     SPECULATED: a main-table literal of the block's speculated
 *               length (literal/length tables of dynamic blocks only)
 *   bit  31     LITERAL (literal/length and code-length tables only, where
 *               values stay below 2^14); a distance base may use bits 30
 *               and 31, so distance entries are never classified by
 *               XX_HE_KIND
 * A length/distance entry is consumed with one shift; its extra bits are
 * (saved & mask(total)) >> code length of the bits before the shift.
 * Unused codes of an incomplete code resolve to BAD. */
#define XX_ARRAYSIZE_DEC(a) (sizeof(a) / sizeof((a)[0]))
#define XX_HK_LIT 0U
#define XX_HK_LEN 1U
#define XX_HK_EOB 2U
#define XX_HK_SUB 3U
#define XX_HK_BAD 4U
#define XX_HF_LIT 0x80000000U
#define XX_HF_EOB 0x2000U
#define XX_HF_SUB 0x4000U
#define XX_HF_EXC 0x8000U
#define XX_HF_SPEC 0x40000000U
/* Symbol information: flags, value and the extra-bit count in bits 0..5;
 * a table entry adds the code length to the total and the length field. */
#define XX_HI_LIT(v) (XX_HF_LIT | ((uint32_t)(v) << 16))
#define XX_HI_LEN(extra, base) ((uint32_t)(extra) | ((uint32_t)(base) << 16))
#define XX_HI_EOB (XX_HF_EXC | XX_HF_EOB)
#define XX_HI_BAD XX_HF_EXC
#define XX_HE_ADD_LEN(info, len) ((info) + (uint32_t)(len) + ((uint32_t)(len) << 8))
#define XX_HE_SUBLINK(table_bits, sub_bits, offset) \
    (XX_HF_EXC | XX_HF_SUB | (uint32_t)(table_bits) | ((uint32_t)(sub_bits) << 8) | ((uint32_t)(offset) << 16))
#define XX_HE_TOTAL(e) ((e) & 63U)
#define XX_HE_CL(e) (((e) >> 8) & 31U)
#define XX_HE_EXTRA(e) (XX_HE_TOTAL(e) - XX_HE_CL(e))

static inline unsigned XX_HE_KIND(uint32_t e)
{
    if (e & XX_HF_LIT) return XX_HK_LIT;
    if (!(e & XX_HF_EXC)) return XX_HK_LEN;
    if (e & XX_HF_SUB) return XX_HK_SUB;
    if (e & XX_HF_EOB) return XX_HK_EOB;
    return XX_HK_BAD;
}

#define XX_DEC_LIT_BITS 11U
#define XX_DEC_DIST_BITS 9U
#define XX_DEC_CLEN_BITS 7U
/* The literal length the speculative fast loop is built for, and the
 * share of the code space (in 16ths) its literals must cover. */
#define XX_DEC_SPEC_BITS 8U
#ifndef XX_DEC_SPEC_MIN16
#define XX_DEC_SPEC_MIN16 15U
#endif
#define XX_DEC_LIT_MASK ((1U << XX_DEC_LIT_BITS) - 1U)
#define XX_DEC_DIST_MASK ((1U << XX_DEC_DIST_BITS) - 1U)
/* Every long code shares its main-table prefix with at most all other long
 * codes, and one sub-table spans at most 2^(15 - main bits) slots, so these
 * bounds also hold for the incomplete codes that are accepted. */
#define XX_DEC_LIT_SIZE ((1U << XX_DEC_LIT_BITS) + 288U * (1U << (XX_DEFLATE_MAX_BITS - XX_DEC_LIT_BITS)))
#define XX_DEC_DIST_SIZE ((1U << XX_DEC_DIST_BITS) + 32U * (1U << (XX_DEFLATE_MAX_BITS - XX_DEC_DIST_BITS)))
#define XX_DEC_CLEN_SIZE (1U << XX_DEC_CLEN_BITS)

typedef struct {
    uint32_t lit[XX_DEC_LIT_SIZE];
    uint32_t dist[XX_DEC_DIST_SIZE];
    uint32_t fixed_lit[1U << XX_DEC_LIT_BITS];   /* fixed codes are <= 9 bits */
    uint32_t fixed_dist[1U << XX_DEC_DIST_BITS]; /* fixed codes are 5 bits */
    uint32_t clen[XX_DEC_CLEN_SIZE];             /* code length codes are <= 7 bits */
    uint32_t lit_info[288];
    uint32_t dist_info[32];
    uint32_t clen_info[XX_DEFLATE_MAX_CLEN_CODES];
    uint8_t lit_max, dist_max, fixed_lit_max, fixed_dist_max, clen_max;
    bool fixed_built;
} xx_dec_state;

static const uint8_t g_cll_order[XX_DEFLATE_MAX_CLEN_CODES] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static const uint16_t g_deflate_length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t g_deflate_length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const uint16_t g_deflate_dist_base[32] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
    32769, 49153
};
static const uint8_t g_deflate_dist_extra[32] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
    14, 14
};

/* Symbol meanings: length code 285 is base 3 + 16 extra bits in Deflate64
 * (258 otherwise); 286/287 and, outside Deflate64, distances 30/31 are
 * invalid when decoded. */
static void xx_dec_state_init(xx_dec_state *st, bool is_deflate64)
{
    unsigned i;
    for (i = 0U; i < 256U; ++i) st->lit_info[i] = XX_HI_LIT(i);
    st->lit_info[256] = XX_HI_EOB;
    for (i = 0U; i < 28U; ++i) st->lit_info[257U + i] = XX_HI_LEN(g_deflate_length_extra[i], g_deflate_length_base[i]);
    st->lit_info[285] = is_deflate64 ? XX_HI_LEN(16, 3) : XX_HI_LEN(0, 258);
    st->lit_info[286] = XX_HI_BAD;
    st->lit_info[287] = XX_HI_BAD;
    for (i = 0U; i < 32U; ++i) st->dist_info[i] = XX_HI_LEN(g_deflate_dist_extra[i], g_deflate_dist_base[i]);
    if (!is_deflate64) {
        st->dist_info[30] = XX_HI_BAD;
        st->dist_info[31] = XX_HI_BAD;
    }
    for (i = 0U; i < XX_DEFLATE_MAX_CLEN_CODES; ++i) st->clen_info[i] = XX_HI_LIT(i);
    st->fixed_built = false;
}

static const uint8_t g_dec_rev8[256] = {
    0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
    0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
    0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
    0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
    0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
    0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
    0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
    0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
    0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
    0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
    0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
    0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
    0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
    0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
    0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF,
};

/* The low len (1..15) bits of code, bit-reversed; no loop-carried chain. */
static inline unsigned xx_dec_reverse(unsigned code, unsigned len)
{
    unsigned r = ((unsigned)g_dec_rev8[code & 0xFFU] << 8) | g_dec_rev8[(code >> 8) & 0xFFU];
    return r >> (16U - len);
}

/* Same acceptance as the canonical builder it replaces: lengths <= 15 and a
 * code space that is not over-subscribed; incomplete codes are accepted and
 * their unused slots decode as BAD. Main-table literals of length spec
 * (1..table_bits; 0 = none) are flagged XX_HF_SPEC (a literal's info has
 * XX_HF_LIT = 2 * XX_HF_SPEC, other symbols have neither bit).
 * The symbols are counted and sorted in four contiguous quarters with their
 * own counters, so that runs of equal lengths do not serialise on one
 * counter's store-to-load chain; the quarters are placed in order, so each
 * length group stays in symbol order. A code is first[len] plus its rank in
 * its length group. */
static bool xx_htab_build(uint32_t *table, size_t capacity, unsigned table_bits, const uint8_t *lengths, unsigned num_symbols, const uint32_t *info,
                          unsigned spec, uint8_t *out_max_bits)
{
    uint16_t qcount[4][XX_DEFLATE_MAX_BITS + 1];
    uint16_t qpos[4][XX_DEFLATE_MAX_BITS + 1];
    uint16_t count[XX_DEFLATE_MAX_BITS + 1];
    uint16_t start[XX_DEFLATE_MAX_BITS + 2];
    uint16_t first[XX_DEFLATE_MAX_BITS + 1];
    uint16_t sorted[288];
    unsigned qb[5];
    uint32_t code_space = 0U, cur = 0U;
    unsigned i, j, len, max_bits = 0U, total, main_size = 1U << table_bits, any = 0U, qmin;
    size_t next_sub = main_size;
    bool complete;

#define XX_HTAB_CODE(k, l) ((unsigned)first[(l)] + ((unsigned)(k) - start[(l)]))
    {
        unsigned q = (num_symbols + 3U) / 4U;
        for (j = 0U; j < 5U; ++j) qb[j] = j * q < num_symbols ? j * q : num_symbols;
    }
    qmin = qb[4] - qb[3]; /* the last quarter is the shortest */
    xx_rt_memset(qcount, 0, sizeof(qcount));
    for (i = 0U; i < qmin; ++i) {
        unsigned a = lengths[qb[0] + i], b = lengths[qb[1] + i], c = lengths[qb[2] + i], d = lengths[qb[3] + i];
        any |= a | b | c | d;
        qcount[0][a & 15U]++;
        qcount[1][b & 15U]++;
        qcount[2][c & 15U]++;
        qcount[3][d & 15U]++;
    }
    for (j = 0U; j < 3U; ++j)
        for (i = qb[j] + qmin; i < qb[j + 1U]; ++i) {
            any |= lengths[i];
            qcount[j][lengths[i] & 15U]++;
        }
    if (any > XX_DEFLATE_MAX_BITS) return false;
    count[0] = 0U;
    for (len = 1U; len <= XX_DEFLATE_MAX_BITS; ++len) {
        count[len] = (uint16_t)(qcount[0][len] + qcount[1][len] + qcount[2][len] + qcount[3][len]);
        code_space = (code_space << 1) + count[len];
        if (count[len] != 0U) max_bits = len;
    }
    if (code_space > (1U << XX_DEFLATE_MAX_BITS)) return false;

    start[1] = 0U;
    for (len = 1U; len <= XX_DEFLATE_MAX_BITS; ++len) start[len + 1U] = (uint16_t)(start[len] + count[len]);
    total = start[XX_DEFLATE_MAX_BITS + 1U];
    first[0] = 0U;
    for (len = 1U; len <= XX_DEFLATE_MAX_BITS; ++len) {
        cur = (cur + count[len - 1U]) << 1;
        first[len] = (uint16_t)cur;
    }
    /* Unused (length 0) symbols are parked after the coded ones. */
    for (len = 0U; len <= XX_DEFLATE_MAX_BITS; ++len) {
        unsigned p = len != 0U ? start[len] : total;
        for (j = 0U; j < 4U; ++j) {
            qpos[j][len] = (uint16_t)p;
            p += qcount[j][len];
        }
    }
    for (i = 0U; i < qmin; ++i) {
        sorted[qpos[0][lengths[qb[0] + i]]++] = (uint16_t)(qb[0] + i);
        sorted[qpos[1][lengths[qb[1] + i]]++] = (uint16_t)(qb[1] + i);
        sorted[qpos[2][lengths[qb[2] + i]]++] = (uint16_t)(qb[2] + i);
        sorted[qpos[3][lengths[qb[3] + i]]++] = (uint16_t)(qb[3] + i);
    }
    for (j = 0U; j < 3U; ++j)
        for (i = qb[j] + qmin; i < qb[j + 1U]; ++i) sorted[qpos[j][lengths[i]]++] = (uint16_t)i;

    /* A complete code assigns every slot; only incomplete ones need BAD fill. */
    complete = code_space == (1U << XX_DEFLATE_MAX_BITS);
    i = 0U;
    if (!complete) {
        unsigned k;
        for (k = 0U; k < main_size; ++k) table[k] = XX_HI_BAD;
    } else if (start[table_bits + 1U] != 0U) {
        /* Complete code: the main-table codes of each length are stored
         * once each at their bit-reversed codeword in the first 2^len
         * slots, and that prefix is doubled whenever the length grows
         * (libdeflate's fill), so the stores are sequential copies instead
         * of 2^(bits - len) strided ones. The slots of long-code prefixes
         * are set by the sub-table links below. */
        unsigned end;
        len = lengths[sorted[0]];
        end = 1U << len;
        for (;;) {
            unsigned k, kend = start[len + 1U], code = first[len];
            uint32_t flag = len == spec ? XX_HF_SPEC : 0U;
            for (k = start[len]; k < kend; ++k, ++code) {
                uint32_t sym_info = info[sorted[k]];
                table[xx_dec_reverse(code, len)] = XX_HE_ADD_LEN(sym_info, len) | ((sym_info >> 1) & flag);
            }
            if (++len > table_bits) break;
            memcpy(table + end, table, end * sizeof(*table));
            end <<= 1;
        }
        i = start[table_bits + 1U];
    }

    for (; i < total;) {
        unsigned sym = sorted[i];
        len = lengths[sym];
        if (len <= table_bits) {
            unsigned idx;
            uint32_t entry = XX_HE_ADD_LEN(info[sym], len) | ((info[sym] >> 1) & (len == spec ? XX_HF_SPEC : 0U));
            for (idx = xx_dec_reverse(XX_HTAB_CODE(i, len), len); idx < main_size; idx += 1U << len) table[idx] = entry;
            ++i;
        } else {
            /* Canonical long codes sharing a main-table prefix are adjacent;
             * the last of the group is its longest. */
            unsigned prefix = XX_HTAB_CODE(i, len) >> (len - table_bits);
            unsigned last = i, sub_bits, sub_size, k;
            while (last + 1U < total) {
                unsigned next_len = lengths[sorted[last + 1U]];
                if ((XX_HTAB_CODE(last + 1U, next_len) >> (next_len - table_bits)) != prefix) break;
                ++last;
            }
            sub_bits = lengths[sorted[last]] - table_bits;
            sub_size = 1U << sub_bits;
            if (next_sub + sub_size > capacity) return false; /* unreachable by the size bounds */
            if (!complete) {
                for (k = 0U; k < sub_size; ++k) table[next_sub + k] = XX_HI_BAD;
            }
            table[xx_dec_reverse(prefix, table_bits)] = XX_HE_SUBLINK(table_bits, sub_bits, next_sub);
            for (; i <= last; ++i) {
                unsigned idx, rest;
                sym = sorted[i];
                len = lengths[sym];
                rest = len - table_bits;
                for (idx = xx_dec_reverse(XX_HTAB_CODE(i, len) & ((1U << rest) - 1U), rest); idx < sub_size; idx += 1U << rest)
                    table[next_sub + idx] = XX_HE_ADD_LEN(info[sym], rest);
            }
            next_sub += sub_size;
        }
    }
#undef XX_HTAB_CODE
    *out_max_bits = (uint8_t)max_bits;
    return true;
}

/* Resolves the code at the bottom of bits; *length receives its full length. */
static inline uint32_t xx_htab_resolve(const uint32_t *table, unsigned table_bits, uint64_t bits, unsigned *length)
{
    uint32_t entry = table[(uint32_t)bits & ((1U << table_bits) - 1U)];
    if (entry & XX_HF_SUB) {
        entry = table[(entry >> 16) + ((uint32_t)(bits >> table_bits) & ((1U << XX_HE_CL(entry)) - 1U))];
        *length = table_bits + XX_HE_CL(entry);
    } else {
        *length = XX_HE_CL(entry);
    }
    return entry;
}

/* Checked decode with the reader's refill pattern of the previous decoder:
 * an opportunistic 11-bit refill, and a refill to the longest code length
 * only for codes longer than 11 bits or a short final remainder. A final
 * code may use fewer bits than a full lookup; missing input bits never
 * complete a code. */
static uint32_t xx_htab_decode(const uint32_t *table, unsigned table_bits, unsigned max_bits, xx_bit_reader *br)
{
    uint32_t entry;
    unsigned len;
    if (br->bit_count < XX_DEFLATE_FAST_BITS) (void)xx_br_refill(br, XX_DEFLATE_FAST_BITS);
    entry = xx_htab_resolve(table, table_bits, br->bit_buf, &len);
    if (XX_HE_KIND(entry) != XX_HK_BAD && len <= XX_DEFLATE_FAST_BITS && (int)len <= br->bit_count) {
        xx_br_drop(br, (int)len);
        return entry;
    }
    if (br->bit_count < (int)max_bits) (void)xx_br_refill(br, (int)max_bits);
    entry = xx_htab_resolve(table, table_bits, br->bit_buf, &len);
    if (XX_HE_KIND(entry) != XX_HK_BAD && (int)len <= br->bit_count) {
        xx_br_drop(br, (int)len);
        return entry;
    }
    br->error = true;
    return XX_HI_BAD;
}

static void xx_dec_build_fixed(xx_dec_state *st)
{
    uint8_t lit_lens[288], dist_lens[32];
    int i;
    for (i = 0; i <= 143; ++i) lit_lens[i] = 8;
    for (i = 144; i <= 255; ++i) lit_lens[i] = 9;
    for (i = 256; i <= 279; ++i) lit_lens[i] = 7;
    for (i = 280; i <= 287; ++i) lit_lens[i] = 8;
    for (i = 0; i < 32; ++i) dist_lens[i] = 5;
    (void)xx_htab_build(st->fixed_lit, XX_ARRAYSIZE_DEC(st->fixed_lit), XX_DEC_LIT_BITS, lit_lens, 288U, st->lit_info, 0U, &st->fixed_lit_max);
    (void)xx_htab_build(st->fixed_dist, XX_ARRAYSIZE_DEC(st->fixed_dist), XX_DEC_DIST_BITS, dist_lens, 32U, st->dist_info, 0U, &st->fixed_dist_max);
    st->fixed_built = true;
}

/* ========================================================================= */
/* --- Linear Output                                                     --- */
/* ========================================================================= */

/* Memory destinations without a preset dictionary are decoded in place:
 * the destination itself is the history window. Device, count-only and
 * dictionary decodes use one linear buffer of window + chunk bytes; when it
 * fills, pending bytes go to the sink and the last window of history moves
 * to the front. limit never passes the memory capacity, so a capacity
 * overflow is detected at the literal or match that causes it. */
#define XX_DEC_OUT_CHUNK (256U * 1024U)
#define XX_DEC_COPY_SLACK 32U
#define XX_DEC_FAST_OUT (2U + 258U + XX_DEC_COPY_SLACK + 28U)
#define XX_DEC_FAST_IN 24U

typedef struct {
    xx_io_device *dev;
    uint8_t *mem_dst;
    size_t mem_cap;
    size_t mem_written; /* bytes moved to mem_dst (buffered mode) */
    uint8_t *base;      /* history starts here */
    uint8_t *pos;       /* next output byte */
    uint8_t *limit;     /* pos never passes it */
    uint8_t *buf_end;
    uint8_t *emit;      /* first byte not yet handed to the sink */
    int64_t emitted;    /* bytes handed to the sink */
    size_t win_size;
    size_t dict_size;
    bool direct;
    bool capped;        /* buffered mode with a memory sink */
    bool error;
} xx_out_lin;

static void xx_out_set_limit(xx_out_lin *out)
{
    out->limit = out->buf_end;
    if (out->capped) {
        size_t room = out->mem_cap - out->mem_written;
        if (room < (size_t)(out->buf_end - out->emit)) out->limit = out->emit + room;
    }
}

static bool xx_out_lin_init(xx_out_lin *out, xx_io_device *dev, uint8_t *mem_dst, size_t mem_cap, size_t win_size, const uint8_t *dictionary,
                            size_t dictionary_size)
{
    xx_rt_memset(out, 0, sizeof(*out));
    out->dev = dev;
    out->mem_dst = mem_dst;
    out->mem_cap = mem_cap;
    out->win_size = win_size;
    out->dict_size = dictionary_size;
    if (!dev && mem_dst && dictionary_size == 0U) {
        /* A capacity beyond the address space (an "unbounded" caller) is
         * clamped so the end pointer stays representable. */
        uintptr_t span = UINTPTR_MAX - (uintptr_t)mem_dst;
        out->direct = true;
        out->base = out->pos = out->emit = mem_dst;
        out->buf_end = out->limit = mem_dst + ((uintptr_t)mem_cap > span ? (size_t)span : mem_cap);
        return true;
    }
    out->base = (uint8_t *)xx_mem_alloc(win_size + XX_DEC_OUT_CHUNK);
    if (!out->base) return false;
    if (dictionary_size != 0U) xx_rt_memcpy(out->base, dictionary, dictionary_size);
    out->pos = out->emit = out->base + dictionary_size;
    out->buf_end = out->base + win_size + XX_DEC_OUT_CHUNK;
    out->capped = !dev && mem_dst != NULL;
    xx_out_set_limit(out);
    return true;
}

static void xx_out_lin_free(xx_out_lin *out)
{
    if (!out->direct && out->base) xx_mem_free(out->base);
    out->base = NULL;
}

static inline int64_t xx_out_total(const xx_out_lin *out)
{
    return out->emitted + (int64_t)(out->pos - out->emit);
}

/* Hands pending bytes to the sink. */
static bool xx_out_emit(xx_out_lin *out)
{
    size_t pending = (size_t)(out->pos - out->emit);
    if (out->direct || pending == 0U) return !out->error;
    if (out->dev) {
        if (out->error || xx_io_write(out->dev, out->emit, pending) != (ssize_t)pending) {
            out->error = true;
            return false;
        }
    } else if (out->mem_dst) {
        xx_rt_memcpy(out->mem_dst + out->mem_written, out->emit, pending);
        out->mem_written += pending;
    }
    out->emitted += (int64_t)pending;
    out->emit = out->pos;
    return true;
}

/* Ensures need bytes of room at pos. False without error: the memory
 * capacity is reached (the caller reports the overflow). */
static bool xx_out_make_room(xx_out_lin *out, size_t need)
{
    size_t keep;
    if ((size_t)(out->limit - out->pos) >= need) return true;
    if (out->direct || out->error) return false;
    if (out->capped && (out->mem_cap - out->mem_written) - (size_t)(out->pos - out->emit) < need) return false;
    if (!xx_out_emit(out)) return false;
    keep = (size_t)(out->pos - out->base);
    if (keep > out->win_size) keep = out->win_size;
    memmove(out->base, out->pos - keep, keep);
    out->pos = out->emit = out->base + keep;
    xx_out_set_limit(out);
    return (size_t)(out->limit - out->pos) >= need;
}

/* Byte-exact LZ77 copy; writes nothing past dst + length. */
static void xx_copy_exact(uint8_t *dst, size_t distance, size_t length)
{
    const uint8_t *src = dst - distance;
    if (distance >= length) {
        xx_rt_memcpy(dst, src, length);
    } else if (distance >= 8U) {
        while (length >= 8U) {
            uint64_t word;
            memcpy(&word, src, sizeof(word));
            memcpy(dst, &word, sizeof(word));
            src += 8U;
            dst += 8U;
            length -= 8U;
        }
        while (length-- != 0U) *dst++ = *src++;
    } else {
        while (length-- != 0U) *dst++ = *src++;
    }
}

/* A match that does not fit: the previous ring decoder stored it one
 * window-wrap span at a time and failed at the first span beyond the
 * capacity, so the same prefix is kept. */
static bool xx_out_overflow_match(xx_out_lin *out, size_t distance, size_t length)
{
    size_t logical = out->dict_size + (size_t)xx_out_total(out);
    size_t chunk = out->win_size - (logical & (out->win_size - 1U));
    while (!out->error && length != 0U) {
        if (chunk > length) chunk = length;
        if (!xx_out_make_room(out, chunk)) break;
        xx_copy_exact(out->pos, distance, chunk);
        out->pos += chunk;
        length -= chunk;
        chunk = out->win_size;
    }
    out->error = true;
    return false;
}

static bool xx_out_copy_checked(xx_out_lin *out, size_t distance, size_t length)
{
    if (!xx_out_make_room(out, length)) return xx_out_overflow_match(out, distance, length);
    xx_copy_exact(out->pos, distance, length);
    out->pos += length;
    return true;
}

/* ========================================================================= */
/* --- Match Copy Backends                                               --- */
/* ========================================================================= */

/* Each backend may write up to XX_DEC_COPY_SLACK - 1 bytes past
 * dst + length; the fast loop guarantees that room. distance is at least 1
 * and never exceeds the bytes before dst. */
static XX_DEFLATE_DEC_INLINE uint64_t xx_dec_load64(const uint8_t *p)
{
    uint64_t v;
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    memcpy(&v, p, sizeof(v));
#else
    v = (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
        ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
#endif
    return v;
}

static XX_DEFLATE_DEC_INLINE void xx_dec_copy8(uint8_t *dst, const uint8_t *src)
{
    uint64_t v;
    memcpy(&v, src, sizeof(v));
    memcpy(dst, &v, sizeof(v));
}

/* Distances below 8: replicate the period into the first 8 bytes so that
 * the remaining copy runs at a distance of at least 8 (LZ4 style). */
static XX_DEFLATE_DEC_INLINE void xx_dec_copy_short(uint8_t *dst, size_t distance, size_t length)
{
    static const uint8_t inc[8] = {0, 1, 2, 1, 0, 4, 4, 4};
    static const int8_t dec[8] = {0, 0, 0, -1, -4, 1, 2, 3};
    const uint8_t *src = dst - distance;
    uint8_t *end = dst + length;
    if (distance == 1U) {
        uint64_t v = (uint64_t)src[0] * UINT64_C(0x0101010101010101);
        do {
            memcpy(dst, &v, sizeof(v));
            dst += 8U;
        } while (dst < end);
        return;
    }
    if (distance < 8U) {
        uint32_t word;
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        src += inc[distance];
        memcpy(&word, src, sizeof(word));
        memcpy(dst + 4, &word, sizeof(word));
        src -= dec[distance];
        dst += 8U;
    }
    while (dst < end) {
        xx_dec_copy8(dst, src);
        dst += 8U;
        src += 8U;
    }
}

static XX_DEFLATE_DEC_INLINE void xx_dec_copy_scalar(uint8_t *dst, size_t distance, size_t length)
{
    if (distance >= 8U) {
        const uint8_t *src = dst - distance;
        uint8_t *end = dst + length;
        do {
            xx_dec_copy8(dst, src);
            dst += 8U;
            src += 8U;
        } while (dst < end);
    } else {
        xx_dec_copy_short(dst, distance, length);
    }
}


/* Byte i of row d is i % d: one shuffle of the d history bytes yields a
 * vector of the repeating pattern, which is stored every g_dec_step[d]
 * bytes (the largest multiple of d not above 16). */
#if defined(XX_DEFLATE_DEC_X86) || defined(XX_DEFLATE_DEC_NEON)
static const uint8_t g_dec_pattern[16][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
    {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},
    {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},
    {0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0},
    {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3},
    {0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1},
    {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 6},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 1, 2, 3, 4},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 2, 3},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 1, 2},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0},
};
static const uint8_t g_dec_step[16] = {16, 16, 16, 15, 16, 15, 12, 14, 16, 9, 10, 11, 12, 13, 14, 15};
#endif

#ifdef XX_DEFLATE_DEC_X86
XX_DEFLATE_DEC_TARGET_SSE2
static XX_DEFLATE_DEC_INLINE void xx_dec_copy_sse2(uint8_t *dst, size_t distance, size_t length)
{
    if (distance >= 16U) {
        const uint8_t *src = dst - distance;
        uint8_t *end = dst + length;
        do {
            _mm_storeu_si128((__m128i *)(void *)dst, _mm_loadu_si128((const __m128i *)(const void *)src));
            dst += 16U;
            src += 16U;
        } while (dst < end);
    } else if (distance == 1U) {
        __m128i value = _mm_set1_epi8((char)dst[-1]);
        uint8_t *end = dst + length;
        do {
            _mm_storeu_si128((__m128i *)(void *)dst, value);
            dst += 16U;
        } while (dst < end);
    } else if (distance >= 8U) {
        xx_dec_copy_scalar(dst, distance, length);
    } else {
        xx_dec_copy_short(dst, distance, length);
    }
}

/* AVX2 implies SSSE3: short periods are replicated with one shuffle. */
XX_DEFLATE_DEC_TARGET_AVX2
static XX_DEFLATE_DEC_INLINE void xx_dec_copy_avx2(uint8_t *dst, size_t distance, size_t length)
{
    const uint8_t *src = dst - distance;
    uint8_t *end = dst + length;
    if (distance >= 16U) {
        /* Most matches fit two 16-byte stores (the second one rereads what
         * the first wrote when distance < 32); longer ones at a distance of
         * 32 or more continue with 32-byte stores. */
        _mm_storeu_si128((__m128i *)(void *)dst, _mm_loadu_si128((const __m128i *)(const void *)src));
        _mm_storeu_si128((__m128i *)(void *)(dst + 16), _mm_loadu_si128((const __m128i *)(const void *)(src + 16)));
        dst += 32U;
        src += 32U;
        if (dst < end) {
            if (distance >= 32U) {
                do {
                    _mm256_storeu_si256((__m256i *)(void *)dst, _mm256_loadu_si256((const __m256i *)(const void *)src));
                    dst += 32U;
                    src += 32U;
                } while (dst < end);
            } else {
                do {
                    _mm_storeu_si128((__m128i *)(void *)dst, _mm_loadu_si128((const __m128i *)(const void *)src));
                    dst += 16U;
                    src += 16U;
                } while (dst < end);
            }
        }
    } else if (distance == 1U) {
        __m128i value = _mm_set1_epi8((char)src[0]);
        do {
            _mm_storeu_si128((__m128i *)(void *)dst, value);
            dst += 16U;
        } while (dst < end);
    } else {
        __m128i value = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(const void *)src),
                                         _mm_loadu_si128((const __m128i *)(const void *)g_dec_pattern[distance]));
        size_t step = g_dec_step[distance];
        do {
            _mm_storeu_si128((__m128i *)(void *)dst, value);
            dst += step;
        } while (dst < end);
    }
}
#endif

#ifdef XX_DEFLATE_DEC_BMI2
/* The AVX2 copy with 128-bit operations only: Zen 1 splits 256-bit ones
 * anyway, and a loop free of YMM registers needs no VZEROUPPER on its back
 * edge (MSVC places one there before the calls of the loop). The same
 * bytes are stored as by xx_dec_copy_avx2, including the slack. */
XX_DEFLATE_DEC_TARGET_AVX2
static XX_DEFLATE_DEC_INLINE void xx_dec_copy_x128(uint8_t *dst, size_t distance, size_t length)
{
    const uint8_t *src = dst - distance;
    uint8_t *end = dst + length;
    if (distance >= 16U) {
        _mm_storeu_si128((__m128i *)(void *)dst, _mm_loadu_si128((const __m128i *)(const void *)src));
        _mm_storeu_si128((__m128i *)(void *)(dst + 16), _mm_loadu_si128((const __m128i *)(const void *)(src + 16)));
        dst += 32U;
        src += 32U;
        if (dst < end) {
            if (distance >= 32U) {
                do {
                    __m128i a = _mm_loadu_si128((const __m128i *)(const void *)src);
                    __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(src + 16));
                    _mm_storeu_si128((__m128i *)(void *)dst, a);
                    _mm_storeu_si128((__m128i *)(void *)(dst + 16), b);
                    dst += 32U;
                    src += 32U;
                } while (dst < end);
            } else {
                do {
                    _mm_storeu_si128((__m128i *)(void *)dst, _mm_loadu_si128((const __m128i *)(const void *)src));
                    dst += 16U;
                    src += 16U;
                } while (dst < end);
            }
        }
    } else if (distance == 1U) {
        __m128i value = _mm_set1_epi8((char)src[0]);
        do {
            _mm_storeu_si128((__m128i *)(void *)dst, value);
            dst += 16U;
        } while (dst < end);
    } else {
        __m128i value = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(const void *)src),
                                         _mm_loadu_si128((const __m128i *)(const void *)g_dec_pattern[distance]));
        size_t step = g_dec_step[distance];
        do {
            _mm_storeu_si128((__m128i *)(void *)dst, value);
            dst += step;
        } while (dst < end);
    }
}
#endif

#ifdef XX_DEFLATE_DEC_NEON
static XX_DEFLATE_DEC_INLINE void xx_dec_copy_neon(uint8_t *dst, size_t distance, size_t length)
{
    const uint8_t *src = dst - distance;
    uint8_t *end = dst + length;
    if (distance >= 16U) {
        do {
            vst1q_u8(dst, vld1q_u8(src));
            dst += 16U;
            src += 16U;
        } while (dst < end);
    } else {
        uint8x16_t value = vqtbl1q_u8(vld1q_u8(src), vld1q_u8(g_dec_pattern[distance]));
        size_t step = g_dec_step[distance];
        do {
            vst1q_u8(dst, value);
            dst += step;
        } while (dst < end);
    }
}
#endif

/* ========================================================================= */
/* --- Fast Block Loops                                                  --- */
/* ========================================================================= */

typedef int (*xx_dec_fast_fn)(const uint32_t *, const uint32_t *, xx_bit_reader *, xx_out_lin *);

/* Every backend has a plain instance and a speculative one (XXDF_SPEC, for
 * blocks whose literals are mostly XX_DEC_SPEC_BITS long; see the loop). */
typedef struct {
    xx_dec_fast_fn plain;
    xx_dec_fast_fn spec;
} xx_dec_fast_pair;

/* Plain shifts; the BMI2 instances replace them (see the fast loop). */
#define XXDF_SHL(v, n) ((v) << ((n) & 63U))
#define XXDF_SHR(v, e) ((v) >> XX_HE_TOTAL(e))
#define XXDF_EXTRA(e) ((size_t)(((uint32_t)saved & ((1U << XX_HE_TOTAL(e)) - 1U)) >> XX_HE_CL(e)))

#define XXDF_ATTR
#define XXDF_COPY xx_dec_copy_scalar
#define XXDF_NAME xx_dec_fast_scalar
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#define XXDF_SPEC
#define XXDF_NAME xx_dec_fast_scalar_spec
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#undef XXDF_SPEC
#undef XXDF_ATTR
#undef XXDF_COPY

#ifdef XX_DEFLATE_DEC_X86
#define XXDF_ATTR XX_DEFLATE_DEC_TARGET_SSE2
#define XXDF_COPY xx_dec_copy_sse2
#define XXDF_NAME xx_dec_fast_sse2
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#define XXDF_SPEC
#define XXDF_NAME xx_dec_fast_sse2_spec
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#undef XXDF_SPEC
#undef XXDF_ATTR
#undef XXDF_COPY

#define XXDF_ATTR XX_DEFLATE_DEC_TARGET_AVX2
#define XXDF_COPY xx_dec_copy_avx2
#define XXDF_NAME xx_dec_fast_avx2
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#define XXDF_SPEC
#define XXDF_NAME xx_dec_fast_avx2_spec
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#undef XXDF_SPEC
#undef XXDF_ATTR
#undef XXDF_COPY
#endif

#ifdef XX_DEFLATE_DEC_NEON
#define XXDF_ATTR
#define XXDF_COPY xx_dec_copy_neon
#define XXDF_NAME xx_dec_fast_neon
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#define XXDF_SPEC
#define XXDF_NAME xx_dec_fast_neon_spec
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#undef XXDF_SPEC
#undef XXDF_ATTR
#undef XXDF_COPY
#endif

#ifdef XX_DEFLATE_DEC_BMI2
/* The bit-buffer shifts sit on the loop's critical path (table load ->
 * shift -> mask -> next table load). SHRX takes its count modulo 64 from
 * the entry itself, which saves the move/zero-extension into CL. */
#undef XXDF_SHL
#undef XXDF_SHR
#undef XXDF_EXTRA
#if defined(_MSC_VER) && !defined(__clang__)
#define XXDF_SHL(v, n) _shlx_u64((v), (unsigned)(n))
#define XXDF_SHR(v, e) _shrx_u64((v), (unsigned)(e))
/* BZHI keeps the low (e & 0xFF) = total bits; SHRX by (e >> 8) mod 32
 * drops the code length. */
#define XXDF_EXTRA(e) ((size_t)_shrx_u32(_bzhi_u32((uint32_t)saved, (e)), (e) >> 8))
#else
/* GCC and Clang have no SHLX/SHRX intrinsics; with the BMI2 target they
 * emit them for masked shift counts. */
#define XXDF_SHL(v, n) ((v) << ((n) & 63U))
#define XXDF_SHR(v, e) ((v) >> ((e) & 63U))
#define XXDF_EXTRA(e) ((size_t)(_bzhi_u32((uint32_t)saved, (e)) >> (((e) >> 8) & 31U)))
#endif
#define XXDF_ATTR XX_DEFLATE_DEC_TARGET_AVX2_BMI2
#define XXDF_COPY xx_dec_copy_x128
#define XXDF_NAME xx_dec_fast_avx2_bmi2
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#define XXDF_SPEC
#define XXDF_NAME xx_dec_fast_avx2_bmi2_spec
#include "platforms/xx_deflate_dec_fast.inc"
#undef XXDF_NAME
#undef XXDF_SPEC
#undef XXDF_ATTR
#undef XXDF_COPY

/* CPUID.(EAX=7,ECX=0):EBX bit 8; only the immutable capability is cached. */
static bool xx_dec_has_bmi2(void)
{
#if defined(_MSC_VER)
    static volatile long capability = 0;
    long value = _InterlockedCompareExchange(&capability, 0, 0);
    if (value == 0) {
        int info[4];
        bool supported = false;
        __cpuid(info, 0);
        if (info[0] >= 7) {
            __cpuidex(info, 7, 0);
            supported = (info[1] & (1 << 8)) != 0;
        }
        value = supported ? 2 : 1;
        _InterlockedCompareExchange(&capability, value, 0);
    }
    return value == 2;
#elif defined(__GNUC__) || defined(__clang__)
    static int capability = 0;
    int value = __atomic_load_n(&capability, __ATOMIC_RELAXED);
    if (value == 0) {
        unsigned eax, ebx, ecx, edx;
        value = __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) && (ebx & (1U << 8)) ? 2 : 1;
        __atomic_store_n(&capability, value, __ATOMIC_RELAXED);
    }
    return value == 2;
#else
    return false;
#endif
}
#endif

/* Respects the acceleration switches; resolved once per stream. */
static xx_dec_fast_pair xx_dec_select_fast(void)
{
    xx_dec_fast_pair pair;
    pair.plain = xx_dec_fast_scalar;
    pair.spec = xx_dec_fast_scalar_spec;
#ifdef XX_DEFLATE_DEC_X86
#ifdef XX_DEFLATE_DEC_BMI2
    if (xx_is_avx2_enabled() && xx_dec_has_bmi2()) {
        pair.plain = xx_dec_fast_avx2_bmi2;
        pair.spec = xx_dec_fast_avx2_bmi2_spec;
        return pair;
    }
#endif
    if (xx_is_avx2_enabled()) {
        pair.plain = xx_dec_fast_avx2;
        pair.spec = xx_dec_fast_avx2_spec;
        return pair;
    }
    if (xx_is_sse2_enabled()) {
        pair.plain = xx_dec_fast_sse2;
        pair.spec = xx_dec_fast_sse2_spec;
        return pair;
    }
#endif
#ifdef XX_DEFLATE_DEC_NEON
    if (xx_is_neon_enabled()) {
        pair.plain = xx_dec_fast_neon;
        pair.spec = xx_dec_fast_neon_spec;
    }
#endif
    return pair;
}

/* ========================================================================= */
/* --- Decompression Engine Core                                         --- */
/* ========================================================================= */

/* Whether a dynamic block runs the speculative fast loop: its literals of
 * XX_DEC_SPEC_BITS bits carry at least 15/16 of the probability that the
 * code implies (2^-len per codeword), so that three literals in a row have
 * that length most of the time, as in data that barely compresses. Only
 * speed depends on it. Four counters keep the sums independent. */
static bool xx_dec_speculate(const uint8_t *lit_lens)
{
    unsigned i, c0 = 0U, c1 = 0U, c2 = 0U, c3 = 0U;
    for (i = 0U; i < 256U; i += 4U) {
        c0 += lit_lens[i] == XX_DEC_SPEC_BITS;
        c1 += lit_lens[i + 1U] == XX_DEC_SPEC_BITS;
        c2 += lit_lens[i + 2U] == XX_DEC_SPEC_BITS;
        c3 += lit_lens[i + 3U] == XX_DEC_SPEC_BITS;
    }
    return ((uint32_t)(c0 + c1 + c2 + c3) << (XX_DEFLATE_MAX_BITS - XX_DEC_SPEC_BITS)) >= (XX_DEC_SPEC_MIN16 << (XX_DEFLATE_MAX_BITS - 4U));
}

/* Decodes Huffman block symbols up to the end-of-block code. The unchecked
 * loop runs while input and output room allow; near either end each symbol
 * goes through the checked reader, which reproduces the reader calls (and
 * so the end-of-input behaviour) of the previous symbol-at-a-time decoder. */
static bool xx_dec_huffman_block(const uint32_t *lit_table, unsigned lit_max, const uint32_t *dist_table, unsigned dist_max, xx_bit_reader *reader,
                                 xx_out_lin *out, xx_dec_fast_fn fast)
{
    for (;;) {
        uint32_t entry, kind;
        int state = fast(lit_table, dist_table, reader, out);
        if (state > 0) return true;
        if (state < 0) return false;

        entry = xx_htab_decode(lit_table, XX_DEC_LIT_BITS, lit_max, reader);
        kind = XX_HE_KIND(entry);
        if (kind == XX_HK_LIT) {
            if (!xx_out_make_room(out, 1U)) {
                out->error = true;
                return false;
            }
            *out->pos++ = (uint8_t)(entry >> 16);
        } else if (kind == XX_HK_EOB) {
            return true;
        } else if (kind == XX_HK_LEN) {
            uint32_t extra = XX_HE_EXTRA(entry);
            size_t length = (size_t)(entry >> 16) + (extra != 0U ? (size_t)xx_br_read(reader, (int)extra) : 0U);
            size_t distance;
            entry = xx_htab_decode(dist_table, XX_DEC_DIST_BITS, dist_max, reader);
            /* A resolved distance entry is a value or BAD (EXCEPTIONAL). */
            if (entry & XX_HF_EXC) return false;
            extra = XX_HE_EXTRA(entry);
            distance = (size_t)(entry >> 16) + (extra != 0U ? (size_t)xx_br_read(reader, (int)extra) : 0U);
            if (distance == 0U || distance > out->win_size || distance > (size_t)(out->pos - out->base)) return false;
            if (!xx_out_copy_checked(out, distance, length)) return false;
        } else {
            return false;
        }
    }
}

/* XX_DEFLATE_UNPACK_ZLIB_STRICT: whether zlib's inflate_table() takes a
 * code length set. An over-subscribed set never passes. An incomplete one
 * passes only outside the code length code and only when its longest code
 * has one bit (a single code of one bit). A set without any code passes
 * (every lookup in it is an invalid code); the caller decides about the code
 * length code, where that cannot end well. Lengths are at most 15. Called
 * once or three times per dynamic block, in strict mode only. */
static bool xx_dec_zlib_takes_set(const uint8_t *lengths, unsigned num_symbols, bool code_length_code)
{
    uint16_t count[XX_DEFLATE_MAX_BITS + 1];
    unsigned i, len, max_bits = 0U;
    int left = 1;
    xx_rt_memset(count, 0, sizeof(count));
    for (i = 0U; i < num_symbols; ++i) count[lengths[i] & 15U]++;
    for (len = 1U; len <= XX_DEFLATE_MAX_BITS; ++len) {
        left = (left << 1) - (int)count[len];
        if (left < 0) return false;
        if (count[len] != 0U) max_bits = len;
    }
    if (max_bits == 0U) return true;
    return left == 0 || (!code_length_code && max_bits == 1U);
}

bool xx_deflate_decompress_stream_with_flags(xx_bit_reader *reader, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written, bool is_deflate64,
                                             size_t window_size, xx_pd_struct *pd, const uint8_t *dictionary, size_t dictionary_size, unsigned flags)
{
    size_t win_size = xx_deflate_resolve_window(is_deflate64, window_size);
    const bool strict = (flags & XX_DEFLATE_UNPACK_ZLIB_STRICT) != 0U;
    xx_out_lin out;
    xx_dec_state *st;
    xx_dec_fast_pair fast;
    int pd_level = -1;
    bool success = true;
    bool bfinal = false;
    bool stopped;

    if (!reader || win_size == 0U || (dictionary_size != 0U && !dictionary) || dictionary_size > win_size) return false;
    if ((flags & ~XX_DEFLATE_UNPACK_ZLIB_STRICT) != 0U) return false;
    if (!xx_out_lin_init(&out, dst_dev, mem_dst, mem_cap, win_size, dictionary, dictionary_size)) return false;
    st = (xx_dec_state *)xx_mem_alloc(sizeof(*st));
    if (!st) {
        xx_out_lin_free(&out);
        return false;
    }
    xx_dec_state_init(st, is_deflate64);
    fast = xx_dec_select_fast();

    if (pd) {
        pd_level = xx_pd_enter_level(pd, 0, is_deflate64 ? "Unpacking Deflate64" : "Unpacking Deflate");
    }

    while (!bfinal && !reader->error) {
        uint32_t final_bit, btype;
        if (pd && xx_pd_is_stopped(pd)) {
            success = false;
            break;
        }

        final_bit = xx_br_read(reader, 1);
        bfinal = (final_bit != 0);
        btype = xx_br_read(reader, 2);

        if (btype == 0) {
            /* Block Type 0: Stored / Uncompressed. Copied in the chunks of
             * the previous ring window, so a capacity overflow keeps the
             * same prefix. */
            uint16_t len, nlen;
            xx_br_align_byte(reader);
            len = (uint16_t)xx_br_read(reader, 16);
            nlen = (uint16_t)xx_br_read(reader, 16);
            if (len != (uint16_t)(~nlen)) {
                success = false;
                break;
            }
            while (len != 0U) {
                size_t logical = dictionary_size + (size_t)xx_out_total(&out);
                size_t chunk = win_size - (logical & (win_size - 1U));
                if (chunk > len) chunk = len;
                if (!xx_out_make_room(&out, chunk)) {
                    out.error = true;
                    success = false;
                    break;
                }
                if (!xx_br_read_bytes(reader, out.pos, chunk)) {
                    success = false;
                    break;
                }
                out.pos += chunk;
                len = (uint16_t)(len - chunk);
            }
            if (!success) break;

        } else if (btype == 1 || btype == 2) {
            /* Block Type 1 (Fixed) or Type 2 (Dynamic) */
            const uint32_t *lit_table, *dist_table;
            unsigned lit_max, dist_max;
            bool spec = false;

            if (btype == 1) {
                if (!st->fixed_built) xx_dec_build_fixed(st);
                lit_table = st->fixed_lit;
                dist_table = st->fixed_dist;
                lit_max = st->fixed_lit_max;
                dist_max = st->fixed_dist_max;
            } else {
                /* Dynamic Huffman Header */
                uint32_t hlit = xx_br_read(reader, 5) + 257;
                uint32_t hdist = xx_br_read(reader, 5) + 1;
                uint32_t hclen = xx_br_read(reader, 4) + 4;
                uint32_t max_dist_codes = is_deflate64 ? XX_DEFLATE_MAX_DIST_CODES_64 : XX_DEFLATE_MAX_DIST_CODES_STD;
                uint8_t clen_lens[XX_DEFLATE_MAX_CLEN_CODES];
                uint8_t lit_dist_lens[XX_DEFLATE_MAX_LIT_LEN_CODES + XX_DEFLATE_MAX_DIST_CODES_64];
                uint32_t total_codes, code_idx = 0, i;
                uint8_t prev_len = 0;

                if (hlit > XX_DEFLATE_MAX_LIT_LEN_CODES || hdist > max_dist_codes) {
                    success = false;
                    break;
                }

                xx_rt_memset(clen_lens, 0, sizeof(clen_lens));
                for (i = 0; i < hclen; ++i) {
                    clen_lens[g_cll_order[i]] = (uint8_t)xx_br_read(reader, 3);
                }
                /* zlib: "invalid code lengths set" for a code length code
                 * that is over-subscribed or incomplete. One without any
                 * code makes zlib read every length as 0 and stop at the
                 * missing end-of-block code (or at the end of the input);
                 * here it fails at the first length, strict or not. */
                if (strict && !xx_dec_zlib_takes_set(clen_lens, XX_DEFLATE_MAX_CLEN_CODES, true)) {
                    success = false;
                    break;
                }
                if (!xx_htab_build(st->clen, XX_ARRAYSIZE_DEC(st->clen), XX_DEC_CLEN_BITS, clen_lens, XX_DEFLATE_MAX_CLEN_CODES, st->clen_info, 0U, &st->clen_max)) {
                    success = false;
                    break;
                }

                total_codes = hlit + hdist;
                while (code_idx < total_codes) {
                    uint32_t entry = xx_htab_decode(st->clen, XX_DEC_CLEN_BITS, st->clen_max, reader);
                    uint32_t sym = (entry >> 16) & 0xFFU; /* without the LITERAL flag */
                    if (XX_HE_KIND(entry) != XX_HK_LIT) {
                        success = false;
                        break;
                    }
                    if (sym <= 15) {
                        prev_len = (uint8_t)sym;
                        lit_dist_lens[code_idx++] = prev_len;
                    } else if (sym == 16) {
                        uint32_t repeat;
                        if (code_idx == 0) {
                            success = false;
                            break;
                        }
                        repeat = xx_br_read(reader, 2) + 3;
                        if (code_idx + repeat > total_codes) {
                            success = false;
                            break;
                        }
                        while (repeat--) lit_dist_lens[code_idx++] = prev_len;
                    } else if (sym == 17) {
                        uint32_t repeat = xx_br_read(reader, 3) + 3;
                        if (code_idx + repeat > total_codes) {
                            success = false;
                            break;
                        }
                        prev_len = 0;
                        while (repeat--) lit_dist_lens[code_idx++] = 0;
                    } else {
                        uint32_t repeat = xx_br_read(reader, 7) + 11;
                        if (code_idx + repeat > total_codes) {
                            success = false;
                            break;
                        }
                        prev_len = 0;
                        while (repeat--) lit_dist_lens[code_idx++] = 0;
                    }
                }
                if (!success) break;

                /* zlib: "invalid code -- missing end-of-block", "invalid
                 * literal/lengths set", "invalid distances set". The limits
                 * of HLIT and HDIST ("too many length or distance symbols":
                 * 286 and 30; 32 distance codes in Deflate64, as in zlib's
                 * inflateBack9) are checked above in every mode. */
                if (strict && (lit_dist_lens[256] == 0U || !xx_dec_zlib_takes_set(lit_dist_lens, hlit, false) ||
                               !xx_dec_zlib_takes_set(lit_dist_lens + hlit, hdist, false))) {
                    success = false;
                    break;
                }

                spec = xx_dec_speculate(lit_dist_lens);
                if (!xx_htab_build(st->lit, XX_ARRAYSIZE_DEC(st->lit), XX_DEC_LIT_BITS, lit_dist_lens, hlit, st->lit_info, spec ? XX_DEC_SPEC_BITS : 0U,
                                   &st->lit_max) ||
                    !xx_htab_build(st->dist, XX_ARRAYSIZE_DEC(st->dist), XX_DEC_DIST_BITS, lit_dist_lens + hlit, hdist, st->dist_info, 0U, &st->dist_max)) {
                    success = false;
                    break;
                }
                lit_table = st->lit;
                dist_table = st->dist;
                lit_max = st->lit_max;
                dist_max = st->dist_max;
            }

            if (!xx_dec_huffman_block(lit_table, lit_max, dist_table, dist_max, reader, &out, spec ? fast.spec : fast.plain)) {
                success = false;
                break;
            }

        } else {
            /* Invalid / reserved block type 3 */
            success = false;
            break;
        }

        if (pd && pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, (uint64_t)xx_out_total(&out));
        }
    }

    if (reader->error) success = false;
    stopped = pd && xx_pd_is_stopped(pd);
    if (out.dev) {
        /* A cut input still delivers everything decoded to the device. */
        if ((success || (reader->eof && !stopped)) && !xx_out_emit(&out)) success = false;
    } else {
        /* Memory destinations hold all decoded bytes, also after a failure. */
        (void)xx_out_emit(&out);
    }

    if (out_written) {
        *out_written = (size_t)xx_out_total(&out);
    }

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    xx_mem_free(st);
    xx_out_lin_free(&out);
    return success;
}

bool xx_deflate_decompress_stream_with_options(xx_bit_reader *reader, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written, bool is_deflate64,
                                               size_t window_size, xx_pd_struct *pd, const uint8_t *dictionary, size_t dictionary_size)
{
    return xx_deflate_decompress_stream_with_flags(reader, dst_dev, mem_dst, mem_cap, out_written, is_deflate64, window_size, pd, dictionary, dictionary_size, 0U);
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
