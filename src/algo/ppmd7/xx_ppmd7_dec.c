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

/* PPMd7 range decoder and streaming decompressor.
 * Algorithm by Dmitry Shkarin (public domain, 2001).
 * Range coder adaptation by Igor Pavlov (public domain, 2018).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_ppmd7_internal.h"
#include "../../io/xx_io_buffer.h"
#include <string.h>

#define kTopValue (1u << 24)

#if defined(_MSC_VER)
#define PPMD7_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define PPMD7_NOINLINE __attribute__((noinline))
#else
#define PPMD7_NOINLINE
#endif

static bool ppmd7_rd_refill(ppmd7_range_dec *rd)
{
    if (rd->error) return false;
    if (rd->ibuf_pos < rd->ibuf_len) return true;
    size_t want = rd->ibuf_capacity;
    if (rd->remaining >= 0) {
        if (rd->remaining == 0) return false;
        if ((int64_t)want > rd->remaining) want = (size_t)rd->remaining;
    }
    ssize_t got;
    if (rd->dev) {
        got = xx_io_read(rd->dev, rd->ibuf, want);
    } else {
        size_t avail = rd->mem_size - rd->mem_pos;
        got = avail == 0 ? 0 : (ssize_t)(avail < want ? avail : want);
        if (got > 0) {
            xx_rt_memcpy(rd->ibuf, rd->mem + rd->mem_pos, (size_t)got);
            rd->mem_pos += (size_t)got;
        }
    }
    if (got <= 0 || (size_t)got > want) return false;
    if (rd->remaining >= 0) rd->remaining -= got;
    rd->ibuf_pos = 0;
    rd->ibuf_len = (size_t)got;
    return true;
}

PPMD7_NOINLINE static int ppmd7_rd_byte_slow(ppmd7_range_dec *rd)
{
    if (!ppmd7_rd_refill(rd)) {
        rd->error = true;
        return -1;
    }
    return (int)rd->ibuf[rd->ibuf_pos++];
}

static inline uint8_t ppmd7_rd_byte(ppmd7_range_dec *rd)
{
    int b = (rd->ibuf_pos < rd->ibuf_len) ? (int)rd->ibuf[rd->ibuf_pos++] : ppmd7_rd_byte_slow(rd);
    return b < 0 ? 0 : (uint8_t)b;
}

bool ppmd7_rd_init(ppmd7_range_dec *rd, xx_io_device *dev, const uint8_t *mem, size_t mem_size, int64_t remaining)
{
    xx_rt_memset(rd, 0, sizeof(*rd));
    rd->dev = dev;
    rd->io_capacity = xx_get_file_buffer_size();
    if (rd->io_capacity > ((size_t)-1 >> 1)) rd->io_capacity = (size_t)-1 >> 1;
    rd->ibuf = &rd->input_byte;
    rd->ibuf_capacity = 1U;
    rd->mem = mem;
    rd->mem_size = mem_size;
    rd->remaining = remaining;
    rd->code = 0;
    rd->range = 0xFFFFFFFFu;

    if (ppmd7_rd_byte(rd) != 0) return false;
    for (int i = 0; i < 4; i++) rd->code = (rd->code << 8) | ppmd7_rd_byte(rd);
    return !rd->error;
}

static inline void Range_Normalize(ppmd7_range_dec *rd)
{
    if (rd->range < kTopValue) {
        rd->code = (rd->code << 8) | ppmd7_rd_byte(rd);
        rd->range <<= 8;
        if (rd->range < kTopValue) {
            rd->code = (rd->code << 8) | ppmd7_rd_byte(rd);
            rd->range <<= 8;
        }
    }
}

static inline void Range_Decode(ppmd7_range_dec *rd, uint32_t start, uint32_t size)
{
    rd->code -= start * rd->range;
    rd->range *= size;
    Range_Normalize(rd);
}

static inline bool ppmd7_rd_byte_local(ppmd7_range_dec *rd, const uint8_t **cur_p, const uint8_t **end_p, uint8_t *out)
{
    const uint8_t *cur = *cur_p;
    if (cur < *end_p) {
        *cur_p = cur + 1;
        *out = *cur;
        return true;
    }
    rd->ibuf_pos = (size_t)(cur - rd->ibuf);
    int nb = ppmd7_rd_byte_slow(rd);
    if (nb < 0) return false;
    *out = (uint8_t)nb;
    *cur_p = rd->ibuf + rd->ibuf_pos;
    *end_p = rd->ibuf + rd->ibuf_len;
    return true;
}

static inline bool ppmd7_norm_local(ppmd7_range_dec *rd, uint32_t *range_p, uint32_t *code_p, const uint8_t **cur_p, const uint8_t **end_p)
{
    uint32_t range = *range_p;
    if (range < kTopValue) {
        uint8_t b;
        if (!ppmd7_rd_byte_local(rd, cur_p, end_p, &b)) return false;
        uint32_t code = (*code_p << 8) | b;
        range <<= 8;
        if (range < kTopValue) {
            if (!ppmd7_rd_byte_local(rd, cur_p, end_p, &b)) {
                *range_p = range;
                *code_p = code;
                return false;
            }
            code = (code << 8) | b;
            range <<= 8;
        }
        *range_p = range;
        *code_p = code;
    }
    return true;
}

/* charMask[sym] is 0xFF while the symbol is still a candidate and 0 once a
 * higher-order context has escaped past it. */
static inline void ppmd7_mask_states(uint8_t *charMask, const CPpmd_State *s, unsigned count)
{
    if (count & 1) {
        charMask[s->Symbol] = 0;
        s++;
    }
    for (count >>= 1; count != 0; count--) {
        const unsigned a = s[0].Symbol, b = s[1].Symbol;
        s += 2;
        charMask[a] = 0;
        charMask[b] = 0;
    }
}

/* Sum of the frequencies of the states whose symbol is not masked. */
static inline uint32_t ppmd7_esc_sum(const CPpmd_State *q, unsigned n, const uint8_t *charMask)
{
    uint32_t h0 = 0, h1 = 0;
    if (n & 1) {
        h0 = (uint32_t)(q->Freq & charMask[q->Symbol]);
        q++;
    }
    for (n >>= 1; n != 0; n--) {
        const unsigned a = q[0].Symbol, b = q[1].Symbol;
        h0 += (uint32_t)(q[0].Freq & charMask[a]);
        h1 += (uint32_t)(q[1].Freq & charMask[b]);
        q += 2;
    }
    return h0 + h1;
}

#if !defined(PPMD7_NO_ESC_SSSE3) && (defined(_M_X64) || defined(__x86_64__))
#include <tmmintrin.h>
#define PPMD7_ESC_SSSE3 1
#define PPMD7_ESC_SIMD_MIN 64 /* below this the table set-up costs more than it saves */
#if defined(__GNUC__) || defined(__clang__)
#define PPMD7_TARGET_SSSE3 __attribute__((target("ssse3")))
#else
#define PPMD7_TARGET_SSSE3
#endif

/* Same result as ppmd7_esc_sum(), eight states (48 bytes) per step; used only
 * when the CPU has SSSE3 (run-time flag CPpmd7.EscSimd). It reads nothing
 * outside the `n` states. charMask holds only 0x00 / 0xFF bytes.
 * blockSums[j] receives the sum over the first 8 * (j + 1) states, for
 * j < n / 8 (at most 32 entries): the caller uses it to locate the decoded
 * symbol without walking all the states again.
 *
 * The 256 mask bytes are first packed into a 16x16 bit matrix (row = high
 * nibble of the symbol, column = low nibble): rowLo/rowHi give, per row, the
 * bits of columns 0-7 / 8-15. A symbol is unmasked when its row byte has the
 * bit of its column set, which two pairs of PSHUFB look-ups test for 16 bytes
 * at once. */
PPMD7_TARGET_SSSE3 PPMD7_NOINLINE static uint32_t ppmd7_esc_sum_ssse3(const CPpmd_State *q, unsigned n, const uint8_t *charMask, uint32_t *blockSums)
{
    /* Gather Symbol (-> bytes 0..7) and Freq (-> bytes 8..15) of 8 states from three 16-byte loads. */
    const __m128i pick0 = _mm_setr_epi8(0, 6, 12, -1, -1, -1, -1, -1, 1, 7, 13, -1, -1, -1, -1, -1);
    const __m128i pick1 = _mm_setr_epi8(-1, -1, -1, 2, 8, 14, -1, -1, -1, -1, -1, 3, 9, 15, -1, -1);
    const __m128i pick2 = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, 4, 10, -1, -1, -1, -1, -1, -1, 5, 11);
    const __m128i colLo = _mm_setr_epi8(1, 2, 4, 8, 16, 32, 64, (char)-128, 0, 0, 0, 0, 0, 0, 0, 0);
    const __m128i colHi = _mm_setr_epi8(0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 4, 8, 16, 32, 64, (char)-128);
    const __m128i nib = _mm_set1_epi8(0x0F);
    const __m128i zero = _mm_setzero_si128();
    __m128i rowLo, rowHi, acc = zero;
    uint32_t sum;
    {
        uint16_t rows[16];
        unsigned r;
        __m128i a, b;
        const __m128i lowByte = _mm_set1_epi16(0x00FF);
        for (r = 0; r < 16; r++) rows[r] = (uint16_t)_mm_movemask_epi8(_mm_loadu_si128((const __m128i *)(const void *)(charMask + 16 * r)));
        a = _mm_loadu_si128((const __m128i *)(const void *)rows);
        b = _mm_loadu_si128((const __m128i *)(const void *)(rows + 8));
        rowLo = _mm_packus_epi16(_mm_and_si128(a, lowByte), _mm_and_si128(b, lowByte));
        rowHi = _mm_packus_epi16(_mm_srli_epi16(a, 8), _mm_srli_epi16(b, 8));
    }
    for (; n >= 8; n -= 8, q += 8) {
        const uint8_t *bp = (const uint8_t *)q;
        const __m128i x = _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(const void *)bp), pick0),
                                                    _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(const void *)(bp + 16)), pick1)),
                                       _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)(const void *)(bp + 32)), pick2));
        const __m128i hi = _mm_and_si128(_mm_srli_epi16(x, 4), nib);
        const __m128i lo = _mm_and_si128(x, nib);
        const __m128i t = _mm_or_si128(_mm_and_si128(_mm_shuffle_epi8(rowLo, hi), _mm_shuffle_epi8(colLo, lo)),
                                       _mm_and_si128(_mm_shuffle_epi8(rowHi, hi), _mm_shuffle_epi8(colHi, lo)));
        /* bytes 0..7 of `masked`: 0xFF where the symbol is masked; the Freq bytes move down to 0..7 */
        const __m128i masked = _mm_cmpeq_epi8(t, zero);
        acc = _mm_add_epi64(acc, _mm_sad_epu8(_mm_andnot_si128(masked, _mm_srli_si128(x, 8)), zero));
        *blockSums++ = (uint32_t)_mm_cvtsi128_si32(acc);
    }
    sum = (uint32_t)_mm_cvtsi128_si32(acc);
    for (; n != 0; n--, q++) sum += (uint32_t)(q->Freq & charMask[q->Symbol]);
    return sum;
}
#endif

PPMD7_NOINLINE static int ppmd7_decode_escape_loop(CPpmd7 *p, ppmd7_range_dec *rd, uint8_t *charMask)
{
    uint32_t range = rd->range;
    uint32_t code = rd->code;
    const uint8_t *cur = rd->ibuf + rd->ibuf_pos;
    const uint8_t *end = rd->ibuf + rd->ibuf_len;
    CPpmd7_Context *mc = p->MinContext;
    int ret;
#ifdef PPMD7_ESC_SSSE3
    uint32_t blockSums[256 / 8];
#endif

    for (;;) {
        CPpmd_State *s, *stats;
        uint32_t freqSum, escFreq, hiCnt;
        CPpmd_See *see;
        unsigned ns, numMasked = mc->NumStats;
        do {
            p->OrderFall++;
            if (!mc->Suffix) {
                p->MinContext = mc;
                ret = -1;
                goto done;
            }
            mc = Ppmd7_GetContext(p, mc->Suffix);
        } while (mc->NumStats == numMasked);
        p->MinContext = mc;
        ns = mc->NumStats;
        stats = Ppmd7_GetStats(p, mc);

        /* Sum of the frequencies of the symbols that are not masked yet. */
#ifdef PPMD7_ESC_SSSE3
        const bool simd = (ns >= PPMD7_ESC_SIMD_MIN && p->EscSimd);
        if (simd) hiCnt = ppmd7_esc_sum_ssse3(stats, ns, charMask, blockSums);
        else
#endif
            hiCnt = ppmd7_esc_sum(stats, ns, charMask);

        see = Ppmd7_MakeEscFreq(p, numMasked, &escFreq);
        freqSum = escFreq + hiCnt;
        range /= freqSum;
        uint64_t hi_bound = (uint64_t)range * hiCnt;

        if ((uint64_t)code < hi_bound) {
            uint8_t symbol;
            s = stats;
            if (ns - numMasked == 1) {
                /* A single candidate: it owns the whole [0, hiCnt) interval. */
                while (charMask[s->Symbol] == 0) s++;
            } else {
                const uint32_t count = code / range;
                uint32_t acc = 0;
#ifdef PPMD7_ESC_SSSE3
                if (simd) {
                    /* Skip the leading 8-state blocks that end at or below
                     * `count`; the walk below then starts exactly where the
                     * full walk would be at that point. */
                    const unsigned numBlocks = ns >> 3;
                    unsigned j = 0;
                    while (j < numBlocks && blockSums[j] <= count) j++;
                    if (j != 0) acc = blockSums[j - 1];
                    s += (size_t)j * 8;
                }
#endif
                for (;;) {
                    acc += (uint32_t)(s->Freq & charMask[s->Symbol]);
                    if (acc > count) break;
                    s++;
                }
                code -= (acc - s->Freq) * range;
            }
            range *= s->Freq;
            if (!ppmd7_norm_local(rd, &range, &code, &cur, &end)) {
                ret = -2;
                goto done;
            }
            Ppmd_See_Update(see);
            p->FoundState = s;
            symbol = s->Symbol;
            Ppmd7_Update2(p);
            ret = (int)symbol;
            goto done;
        }

        if ((uint64_t)code >= (uint64_t)range * freqSum) {
            ret = -2;
            goto done;
        }

        code -= (uint32_t)hi_bound;
        range *= escFreq;
        if (!ppmd7_norm_local(rd, &range, &code, &cur, &end)) {
            ret = -2;
            goto done;
        }
        see->Summ = (uint16_t)(see->Summ + freqSum);
        ppmd7_mask_states(charMask, stats, ns);
    }

done:
    rd->range = range;
    rd->code = code;
    rd->ibuf_pos = (size_t)(cur - rd->ibuf);
    return ret;
}

PPMD7_NOINLINE static int ppmd7_decode_escape_multi(CPpmd7 *p, ppmd7_range_dec *rd, uint32_t hiCnt)
{
    size_t maskWords[256 / sizeof(size_t)];
    uint8_t *charMask = (uint8_t *)maskWords;
    p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol];
    Range_Decode(rd, hiCnt, p->MinContext->SummFreq - hiCnt);
    if (rd->error) return -2;
    PPMD_SetAllBitsIn256Bytes(maskWords);
    ppmd7_mask_states(charMask, Ppmd7_GetStats(p, p->MinContext), p->MinContext->NumStats);
    return ppmd7_decode_escape_loop(p, rd, charMask);
}

PPMD7_NOINLINE static int ppmd7_decode_escape_bin(CPpmd7 *p, ppmd7_range_dec *rd, uint16_t *prob)
{
    size_t maskWords[256 / sizeof(size_t)];
    uint8_t *charMask = (uint8_t *)maskWords;
    *prob = (uint16_t)PPMD_UPDATE_PROB_1(*prob);
    p->InitEsc = PPMD7_kExpEscape[*prob >> 10];
    PPMD_SetAllBitsIn256Bytes(maskWords);
    charMask[Ppmd7Context_OneState(p->MinContext)->Symbol] = 0;
    p->PrevSuccess = 0;
    return ppmd7_decode_escape_loop(p, rd, charMask);
}

static size_t ppmd7_decode_chunk(CPpmd7 *p, ppmd7_range_dec *rd, uint8_t *outbuf, size_t max_count, int *out_stop_sym)
{
    uint32_t range = rd->range;
    uint32_t code = rd->code;
    const uint8_t *cur = rd->ibuf + rd->ibuf_pos;
    const uint8_t *end = rd->ibuf + rd->ibuf_len;
    uint8_t *out_ptr = outbuf;
    uint8_t *const out_end = outbuf + max_count;

    while (out_ptr < out_end) {
        CPpmd7_Context *min_ctx = p->MinContext;
        int sym;
        if (min_ctx->NumStats != 1) {
            CPpmd_State *s = Ppmd7_GetStats(p, min_ctx);
            uint32_t hiCnt = s->Freq;
            range /= min_ctx->SummFreq;
            if ((uint64_t)code < (uint64_t)range * hiCnt) {
                uint8_t symbol = s->Symbol;
                range *= hiCnt;
                if (!ppmd7_norm_local(rd, &range, &code, &cur, &end)) {
                    *out_stop_sym = -2;
                    goto done;
                }
                p->FoundState = s;
                Ppmd7_Update1_0(p);
                *out_ptr++ = symbol;
                continue;
            }
            uint32_t count = code / range;
            p->PrevSuccess = 0;
            unsigned i = min_ctx->NumStats - 1;
            do {
                if ((hiCnt += (++s)->Freq) > count) {
                    uint8_t symbol = s->Symbol;
                    code -= (hiCnt - s->Freq) * range;
                    range *= s->Freq;
                    if (!ppmd7_norm_local(rd, &range, &code, &cur, &end)) {
                        *out_stop_sym = -2;
                        goto done;
                    }
                    p->FoundState = s;
                    Ppmd7_Update1(p);
                    *out_ptr++ = symbol;
                    goto next_sym;
                }
            } while (--i);

            if (count >= min_ctx->SummFreq) {
                *out_stop_sym = -2;
                goto done;
            }

            rd->range = range;
            rd->code = code;
            rd->ibuf_pos = (size_t)(cur - rd->ibuf);
            sym = ppmd7_decode_escape_multi(p, rd, hiCnt);
            goto on_escape;
        } else {
            uint16_t *prob = Ppmd7_GetBinSumm(p);
            uint32_t newBound = (range >> 14) * *prob;
            if (code < newBound) {
                range = newBound;
                if (!ppmd7_norm_local(rd, &range, &code, &cur, &end)) {
                    *out_stop_sym = -2;
                    goto done;
                }
                *prob = (uint16_t)PPMD_UPDATE_PROB_0(*prob);
                CPpmd_State *s = Ppmd7Context_OneState(min_ctx);
                p->FoundState = s;
                uint8_t symbol = s->Symbol;
                Ppmd7_UpdateBin(p);
                *out_ptr++ = symbol;
                continue;
            } else {
                range -= newBound;
                code -= newBound;
                if (!ppmd7_norm_local(rd, &range, &code, &cur, &end)) {
                    *out_stop_sym = -2;
                    goto done;
                }
                rd->range = range;
                rd->code = code;
                rd->ibuf_pos = (size_t)(cur - rd->ibuf);
                sym = ppmd7_decode_escape_bin(p, rd, prob);
            }
        }
    on_escape:
        if (sym < 0 || rd->error) {
            *out_stop_sym = (rd->error || sym < -1) ? -2 : -1;
            goto done_synced;
        }
        range = rd->range;
        code = rd->code;
        cur = rd->ibuf + rd->ibuf_pos;
        end = rd->ibuf + rd->ibuf_len;
        *out_ptr++ = (uint8_t)sym;
    next_sym:;
    }

done:
    rd->range = range;
    rd->code = code;
    rd->ibuf_pos = (size_t)(cur - rd->ibuf);
done_synced:
    return (size_t)(out_ptr - outbuf);
}

int Ppmd7_DecodeSymbol(CPpmd7 *p, ppmd7_range_dec *rc)
{
    uint8_t sym_byte = 0;
    int stop_sym = 0;
    if (ppmd7_decode_chunk(p, rc, &sym_byte, 1U, &stop_sym) == 1U) {
        return (int)sym_byte;
    }
    return stop_sym;
}

static bool xx_ppmd7_decompress_stream_impl(ppmd7_range_dec *rd, int order, uint32_t mem_size, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap,
                                            size_t *out_written, xx_pd_struct *pd, bool size_defined, uint64_t expected_size)
{
    if (order < PPMD7_MIN_ORDER || order > PPMD7_MAX_ORDER) return false;
    if (mem_size < XX_PPMD7_MIN_MEM_SIZE || mem_size > XX_PPMD7_MAX_MEM_SIZE) return false;

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, 0, "Decompressing PPMd7");
    }

    CPpmd7 ppmd;
    Ppmd7_Construct(&ppmd);
    if (!Ppmd7_Alloc(&ppmd, mem_size)) {
        xx_io_note_alloc_failure();
        if (pd && pd_level >= 0) xx_pd_leave_level(pd, pd_level);
        return false;
    }
    Ppmd7_Init(&ppmd, (unsigned)order);
#ifdef PPMD7_ESC_SSSE3
    ppmd.EscSimd = xx_is_sse42_enabled() ? 1U : 0U; /* SSE4.2 implies SSSE3 */
#endif

    size_t opos = 0;
    uint64_t tot = 0U;
    size_t io_capacity = rd->io_capacity;
    if (io_capacity == 0U) io_capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (io_capacity > ((size_t)-1 >> 1)) io_capacity = (size_t)-1 >> 1;
    uint8_t *staging = (uint8_t *)xx_mem_alloc(io_capacity * 2U);
    uint8_t *outbuf;
    if (!staging) {
        xx_io_note_alloc_failure();
        Ppmd7_Free(&ppmd);
        if (pd && pd_level >= 0) xx_pd_leave_level(pd, pd_level);
        return false;
    }
    rd->ibuf = staging;
    rd->ibuf_capacity = io_capacity;
    rd->ibuf_pos = rd->ibuf_len = 0U;
    outbuf = staging + io_capacity;
    bool ok = true;
    const size_t cancel_quantum = 65536U;

    while (ok) {
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        if (size_defined && tot + (uint64_t)opos == expected_size) break;
        if (mem_dst && tot + (uint64_t)opos >= (uint64_t)mem_cap) {
            if (tot + (uint64_t)opos == 0U && mem_cap == 0U) {
                uint8_t dummy;
                int stop_sym = 0;
                size_t got_syms = ppmd7_decode_chunk(&ppmd, rd, &dummy, 1U, &stop_sym);
                if (rd->error || stop_sym < -1 || got_syms > 0U) ok = false;
            }
            break;
        }

        size_t chunk = io_capacity - opos;
        if (chunk > cancel_quantum) chunk = cancel_quantum;
        if (size_defined) {
            uint64_t rem = expected_size - (tot + (uint64_t)opos);
            if (rem < (uint64_t)chunk) chunk = (size_t)rem;
        }
        if (mem_dst) {
            size_t rem_mem = (size_t)((uint64_t)mem_cap - (tot + (uint64_t)opos));
            if (rem_mem < chunk) chunk = rem_mem;
        }

        int stop_sym = 0;
        size_t n = ppmd7_decode_chunk(&ppmd, rd, outbuf + opos, chunk, &stop_sym);
        opos += n;

        if (opos >= io_capacity) {
            if (dst_dev) {
                ssize_t w = xx_io_write(dst_dev, outbuf, opos);
                if (w < 0 || (size_t)w != opos) {
                    ok = false;
                    break;
                }
            } else if (mem_dst) {
                if (tot + (uint64_t)opos > (uint64_t)mem_cap) {
                    ok = false;
                    break;
                }
                xx_rt_memcpy(mem_dst + (size_t)tot, outbuf, opos);
            }
            tot += (uint64_t)opos;
            opos = 0;
            if (pd && pd_level >= 0) xx_pd_set_current(pd, pd_level, tot);
        }

        if (rd->error || stop_sym < -1) {
            ok = false;
            break;
        }
        if (stop_sym == -1) {
            /* End of stream */
            break;
        }
    }

    if (ok && pd && xx_pd_is_stopped(pd)) {
        ok = false;
    }

    if (ok && opos > 0) {
        if (dst_dev) {
            ssize_t w = xx_io_write(dst_dev, outbuf, opos);
            if (w < 0 || (size_t)w != opos) ok = false;
        } else if (mem_dst) {
            if (tot + (uint64_t)opos <= (uint64_t)mem_cap) xx_rt_memcpy(mem_dst + (size_t)tot, outbuf, opos);
            else ok = false;
        }
        tot += (uint64_t)opos;
        if (pd && pd_level >= 0) xx_pd_set_current(pd, pd_level, tot);
    }

    if (out_written) *out_written = (size_t)tot;
    Ppmd7_Free(&ppmd);
    xx_mem_free(staging);
    rd->ibuf = &rd->input_byte;
    rd->ibuf_capacity = 1U;
    rd->ibuf_pos = rd->ibuf_len = 0U;

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }
    if (ok && pd && xx_pd_is_stopped(pd)) {
        ok = false;
    }

    return ok && (!size_defined || tot == expected_size);
}

bool xx_ppmd7_decompress_stream(ppmd7_range_dec *rd, int order, uint32_t mem_size, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd)
{
    return xx_ppmd7_decompress_stream_impl(rd, order, mem_size, dst_dev, mem_dst, mem_cap, out_written, pd, false, 0U);
}

bool xx_ppmd7_decompress_stream_sized(ppmd7_range_dec *rd, int order, uint32_t mem_size, xx_io_device *destination, uint64_t expected_size, xx_pd_struct *pd)
{
    if (!destination || expected_size > INT64_MAX) return false;
    return xx_ppmd7_decompress_stream_impl(rd, order, mem_size, destination, NULL, 0U, NULL, pd, true, expected_size);
}
