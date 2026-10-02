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
#include <string.h>

#define kTopValue (1u << 24)

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

static uint8_t ppmd7_rd_byte(ppmd7_range_dec *rd)
{
    if (!ppmd7_rd_refill(rd)) { rd->error = true; return 0; }
    return rd->ibuf[rd->ibuf_pos++];
}

bool ppmd7_rd_init(ppmd7_range_dec *rd, xx_io_device *dev,
                   const uint8_t *mem, size_t mem_size, int64_t remaining)
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
    rd->code  = 0;
    rd->range = 0xFFFFFFFFu;

    if (ppmd7_rd_byte(rd) != 0) return false;
    for (int i = 0; i < 4; i++)
        rd->code = (rd->code << 8) | ppmd7_rd_byte(rd);
    return !rd->error;
}

static void Range_Normalize(ppmd7_range_dec *rd)
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

static uint32_t Range_GetThreshold(ppmd7_range_dec *rd, uint32_t total)
{
    return rd->code / (rd->range /= total);
}

static void Range_Decode(ppmd7_range_dec *rd, uint32_t start, uint32_t size)
{
    rd->code -= start * rd->range;
    rd->range *= size;
    Range_Normalize(rd);
}

static uint32_t Range_DecodeBit(ppmd7_range_dec *rd, uint32_t size0)
{
    uint32_t newBound = (rd->range >> 14) * size0;
    uint32_t symbol;
    if (rd->code < newBound) {
        symbol = 0;
        rd->range = newBound;
    } else {
        symbol = 1;
        rd->code -= newBound;
        rd->range -= newBound;
    }
    Range_Normalize(rd);
    return symbol;
}

#define MASK(sym) ((int8_t *)charMask)[sym]

int Ppmd7_DecodeSymbol(CPpmd7 *p, ppmd7_range_dec *rc)
{
    size_t charMask[256 / sizeof(size_t)];
    if (p->MinContext->NumStats != 1) {
        CPpmd_State *s = Ppmd7_GetStats(p, p->MinContext);
        unsigned i;
        uint32_t count, hiCnt;
        if ((count = Range_GetThreshold(rc, p->MinContext->SummFreq)) < (hiCnt = s->Freq)) {
            uint8_t symbol;
            Range_Decode(rc, 0, s->Freq);
            p->FoundState = s;
            symbol = s->Symbol;
            Ppmd7_Update1_0(p);
            return (int)symbol;
        }
        p->PrevSuccess = 0;
        i = p->MinContext->NumStats - 1;
        do {
            if ((hiCnt += (++s)->Freq) > count) {
                uint8_t symbol;
                Range_Decode(rc, hiCnt - s->Freq, s->Freq);
                p->FoundState = s;
                symbol = s->Symbol;
                Ppmd7_Update1(p);
                return (int)symbol;
            }
        } while (--i);

        if (count >= p->MinContext->SummFreq)
            return -2;

        p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol];
        Range_Decode(rc, hiCnt, p->MinContext->SummFreq - hiCnt);
        PPMD_SetAllBitsIn256Bytes(charMask);
        MASK(s->Symbol) = 0;
        i = p->MinContext->NumStats - 1;
        do { MASK((--s)->Symbol) = 0; } while (--i);
    } else {
        uint16_t *prob = Ppmd7_GetBinSumm(p);
        if (Range_DecodeBit(rc, *prob) == 0) {
            uint8_t symbol;
            *prob = (uint16_t)PPMD_UPDATE_PROB_0(*prob);
            symbol = (p->FoundState = Ppmd7Context_OneState(p->MinContext))->Symbol;
            Ppmd7_UpdateBin(p);
            return (int)symbol;
        }
        *prob = (uint16_t)PPMD_UPDATE_PROB_1(*prob);
        p->InitEsc = PPMD7_kExpEscape[*prob >> 10];
        PPMD_SetAllBitsIn256Bytes(charMask);
        MASK(Ppmd7Context_OneState(p->MinContext)->Symbol) = 0;
        p->PrevSuccess = 0;
    }

    for (;;) {
        CPpmd_State *ps[256], *s;
        uint32_t freqSum, count, hiCnt;
        CPpmd_See *see;
        unsigned i, num, numMasked = p->MinContext->NumStats;
        do {
            p->OrderFall++;
            if (!p->MinContext->Suffix)
                return -1;
            p->MinContext = Ppmd7_GetContext(p, p->MinContext->Suffix);
        } while (p->MinContext->NumStats == numMasked);

        hiCnt = 0;
        s = Ppmd7_GetStats(p, p->MinContext);
        i = 0;
        num = p->MinContext->NumStats - numMasked;
        do {
            int k = (int)(MASK(s->Symbol));
            hiCnt += (uint32_t)(s->Freq & k);
            ps[i] = s++;
            i -= (unsigned)k;
        } while (i != num);

        see = Ppmd7_MakeEscFreq(p, numMasked, &freqSum);
        freqSum += hiCnt;
        count = Range_GetThreshold(rc, freqSum);

        if (count < hiCnt) {
            uint8_t symbol;
            CPpmd_State **pps = ps;
            for (hiCnt = 0; (hiCnt += (*pps)->Freq) <= count; pps++);
            s = *pps;
            Range_Decode(rc, hiCnt - s->Freq, s->Freq);
            Ppmd_See_Update(see);
            p->FoundState = s;
            symbol = s->Symbol;
            Ppmd7_Update2(p);
            return (int)symbol;
        }

        if (count >= freqSum)
            return -2;

        Range_Decode(rc, hiCnt, freqSum - hiCnt);
        see->Summ = (uint16_t)(see->Summ + freqSum);
        do {
            MASK(ps[--i]->Symbol) = 0;
        } while (i != 0);
    }
}

static bool xx_ppmd7_decompress_stream_impl(ppmd7_range_dec *rd,
                                int order, uint32_t mem_size,
                                xx_io_device *dst_dev,
                                uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd, bool size_defined,
                                uint64_t expected_size)
{
    if (order < PPMD7_MIN_ORDER || order > PPMD7_MAX_ORDER) return false;
    if (mem_size < XX_PPMD7_MIN_MEM_SIZE ||
        mem_size > XX_PPMD7_MAX_MEM_SIZE) return false;

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, 0, "Decompressing PPMd7");
    }

    CPpmd7 ppmd;
    Ppmd7_Construct(&ppmd);
    if (!Ppmd7_Alloc(&ppmd, mem_size)) {
        if (pd && pd_level >= 0) xx_pd_leave_level(pd, pd_level);
        return false;
    }
    Ppmd7_Init(&ppmd, (unsigned)order);

    size_t opos = 0;
    uint64_t tot = 0U;
    size_t io_capacity = rd->io_capacity;
    uint8_t *staging = io_capacity <= (size_t)-1 / 2U
        ? (uint8_t *)xx_mem_alloc(io_capacity * 2U) : NULL;
    uint8_t *outbuf;
    if (!staging) {
        Ppmd7_Free(&ppmd);
        if (pd && pd_level >= 0) xx_pd_leave_level(pd, pd_level);
        return false;
    }
    rd->ibuf = staging;
    rd->ibuf_capacity = io_capacity;
    rd->ibuf_pos = rd->ibuf_len = 0U;
    outbuf = staging + io_capacity;
    bool ok = true;

    while (ok) {
        if (pd && xx_pd_is_stopped(pd)) { ok = false; break; }
        if (size_defined && tot + (uint64_t)opos == expected_size) break;

        int sym = Ppmd7_DecodeSymbol(&ppmd, rd);
        if (rd->error || sym < -1) {
            ok = false;
            break;
        }
        if (sym == -1) {
            /* End of stream */
            break;
        }

        outbuf[opos++] = (uint8_t)sym;
        if (opos >= io_capacity) {
            if (dst_dev) {
                ssize_t w = xx_io_write(dst_dev, outbuf, opos);
                if (w < 0 || (size_t)w != opos) { ok = false; break; }
            } else if (mem_dst) {
                if (tot + opos > mem_cap) { ok = false; break; }
                xx_rt_memcpy(mem_dst + (size_t)tot, outbuf, opos);
            }
            tot += opos;
            opos = 0;
            if (pd && pd_level >= 0) xx_pd_set_current(pd, pd_level, (uint64_t)tot);
        }

        if (mem_dst && (tot + opos >= mem_cap)) {
            break;
        }
    }

    if (ok && opos > 0) {
        if (dst_dev) {
            ssize_t w = xx_io_write(dst_dev, outbuf, opos);
            if (w < 0 || (size_t)w != opos) ok = false;
        } else if (mem_dst) {
            if (tot + opos <= mem_cap)
                xx_rt_memcpy(mem_dst + (size_t)tot, outbuf, opos);
            else
                ok = false;
        }
        tot += opos;
        if (pd && pd_level >= 0) xx_pd_set_current(pd, pd_level, (uint64_t)tot);
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

    return ok && (!size_defined || tot == expected_size);
}

bool xx_ppmd7_decompress_stream(ppmd7_range_dec *rd, int order,
                                uint32_t mem_size, xx_io_device *dst_dev,
                                uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd) {
    return xx_ppmd7_decompress_stream_impl(rd, order, mem_size, dst_dev,
                                           mem_dst, mem_cap, out_written, pd, false, 0U);
}

bool xx_ppmd7_decompress_stream_sized(ppmd7_range_dec *rd, int order,
                                      uint32_t mem_size, xx_io_device *destination,
                                      uint64_t expected_size, xx_pd_struct *pd) {
    if (!destination || expected_size > INT64_MAX) return false;
    return xx_ppmd7_decompress_stream_impl(rd, order, mem_size, destination,
                                           NULL, 0U, NULL, pd, true, expected_size);
}
