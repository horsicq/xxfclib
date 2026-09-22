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

/* PPMd8 range decoder and streaming decompressor.
 * Algorithm by Dmitry Shkarin (public domain, 2002).
 * Carryless 32-bit rangecoder by Dmitry Subbotin (public domain, 1999).
 * Range coder adaptation by Igor Pavlov (public domain, 2018).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_ppmd8_internal.h"
#include <string.h>

#define kTop (1u << 24)
#define kBot (1u << 15)

static bool ppmd8_rd_refill(ppmd8_range_dec *rd)
{
    if (rd->error) return false;
    if (rd->ibuf_pos < rd->ibuf_len) return true;
    size_t want = sizeof(rd->ibuf);
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
    if (got <= 0) return false;
    if (rd->remaining >= 0) rd->remaining -= got;
    rd->ibuf_pos = 0;
    rd->ibuf_len = (size_t)got;
    return true;
}

static inline uint8_t ppmd8_rd_byte(ppmd8_range_dec *rd)
{
    if (rd->ibuf_pos < rd->ibuf_len) {
        return rd->ibuf[rd->ibuf_pos++];
    }
    if (!ppmd8_rd_refill(rd)) {
        rd->error = true;
        return 0;
    }
    return rd->ibuf[rd->ibuf_pos++];
}

bool ppmd8_rd_init(CPpmd8 *p, ppmd8_range_dec *rd, xx_io_device *dev,
                   const uint8_t *mem, size_t mem_size, int64_t remaining)
{
    xx_rt_memset(rd, 0, sizeof(*rd));
    rd->dev = dev;
    rd->mem = mem;
    rd->mem_size = mem_size;
    rd->remaining = remaining;

    p->Low = 0;
    p->Range = 0xFFFFFFFFu;
    p->Code = 0;
    for (int i = 0; i < 4; i++)
        p->Code = (p->Code << 8) | (uint32_t)ppmd8_rd_byte(rd);
    return !rd->error && (p->Code < 0xFFFFFFFFu);
}

static inline uint32_t RangeDec_GetThreshold(CPpmd8 *p, uint32_t total)
{
    return p->Code / (p->Range /= total);
}

static void RangeDec_Decode(CPpmd8 *p, ppmd8_range_dec *rd, uint32_t start, uint32_t size)
{
    start *= p->Range;
    p->Low += start;
    p->Code -= start;
    p->Range *= size;

    while ((p->Low ^ (p->Low + p->Range)) < kTop ||
           (p->Range < kBot && ((p->Range = (0 - p->Low) & (kBot - 1)), 1))) {
        p->Code = (p->Code << 8) | (uint32_t)ppmd8_rd_byte(rd);
        p->Range <<= 8;
        p->Low <<= 8;
    }
}

#define MASK(sym) ((int8_t *)charMask)[sym]

int Ppmd8_DecodeSymbol(CPpmd8 *p, ppmd8_range_dec *rd)
{
    size_t charMask[256 / sizeof(size_t)];
    if (p->MinContext->NumStats != 0) {
        CPpmd_State *s = Ppmd8_GetStats(p, p->MinContext);
        unsigned i;
        uint32_t count, hiCnt;
        if ((count = RangeDec_GetThreshold(p, p->MinContext->SummFreq)) < (hiCnt = s->Freq)) {
            uint8_t symbol;
            RangeDec_Decode(p, rd, 0, s->Freq);
            p->FoundState = s;
            symbol = s->Symbol;
            Ppmd8_Update1_0(p);
            return (int)symbol;
        }
        p->PrevSuccess = 0;
        i = p->MinContext->NumStats;
        do {
            if ((hiCnt += (++s)->Freq) > count) {
                uint8_t symbol;
                RangeDec_Decode(p, rd, hiCnt - s->Freq, s->Freq);
                p->FoundState = s;
                symbol = s->Symbol;
                Ppmd8_Update1(p);
                return (int)symbol;
            }
        } while (--i);

        if (count >= p->MinContext->SummFreq)
            return -2;
        RangeDec_Decode(p, rd, hiCnt, p->MinContext->SummFreq - hiCnt);
        PPMD_SetAllBitsIn256Bytes(charMask);
        MASK(s->Symbol) = 0;
        i = p->MinContext->NumStats;
        do {
            MASK((--s)->Symbol) = 0;
        } while (--i);
    } else {
        uint16_t *prob = Ppmd8_GetBinSumm(p);
        if (((p->Code / (p->Range >>= 14)) < *prob)) {
            uint8_t symbol;
            RangeDec_Decode(p, rd, 0, *prob);
            *prob = (uint16_t)PPMD_UPDATE_PROB_0(*prob);
            symbol = (p->FoundState = Ppmd8Context_OneState(p->MinContext))->Symbol;
            Ppmd8_UpdateBin(p);
            return (int)symbol;
        }
        RangeDec_Decode(p, rd, *prob, (1 << 14) - *prob);
        *prob = (uint16_t)PPMD_UPDATE_PROB_1(*prob);
        p->InitEsc = PPMD8_kExpEscape[*prob >> 10];
        PPMD_SetAllBitsIn256Bytes(charMask);
        MASK(Ppmd8Context_OneState(p->MinContext)->Symbol) = 0;
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
            p->MinContext = Ppmd8_GetContext(p, p->MinContext->Suffix);
        } while (p->MinContext->NumStats == numMasked);

        hiCnt = 0;
        s = Ppmd8_GetStats(p, p->MinContext);
        i = 0;
        num = p->MinContext->NumStats - numMasked;
        do {
            int k = (int)(MASK(s->Symbol));
            hiCnt += (uint32_t)(s->Freq & k);
            ps[i] = s++;
            i -= (unsigned)k;
        } while (i != num);

        see = Ppmd8_MakeEscFreq(p, numMasked, &freqSum);
        freqSum += hiCnt;
        count = RangeDec_GetThreshold(p, freqSum);

        if (count < hiCnt) {
            uint8_t symbol;
            CPpmd_State **pps = ps;
            for (hiCnt = 0; (hiCnt += (*pps)->Freq) <= count; pps++);
            s = *pps;
            RangeDec_Decode(p, rd, hiCnt - s->Freq, s->Freq);
            Ppmd_See_Update(see);
            p->FoundState = s;
            symbol = s->Symbol;
            Ppmd8_Update2(p);
            return (int)symbol;
        }

        if (count >= freqSum)
            return -2;

        RangeDec_Decode(p, rd, hiCnt, freqSum - hiCnt);
        see->Summ = (uint16_t)(see->Summ + freqSum);
        do {
            MASK(ps[--i]->Symbol) = 0;
        } while (i != 0);
    }
}

bool xx_ppmd8_decompress_stream(ppmd8_range_dec *rd,
                                int64_t uncomp_size,
                                int order, uint32_t mem_mb, int restore_method,
                                xx_io_device *dst_dev,
                                uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd)
{
    if (order < PPMD8_MIN_ORDER || order > PPMD8_MAX_ORDER) return false;
    if (restore_method < 0 || restore_method > 1) return false;
    if (mem_mb < XX_PPMD8_MIN_MEM_MB) mem_mb = XX_PPMD8_MIN_MEM_MB;
    if (mem_mb > XX_PPMD8_MAX_MEM_MB) mem_mb = XX_PPMD8_MAX_MEM_MB;

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, uncomp_size > 0 ? (uint64_t)uncomp_size : 0, "Decompressing PPMd8");
    }

    CPpmd8 ppmd;
    Ppmd8_Construct(&ppmd);
    if (!Ppmd8_Alloc(&ppmd, mem_mb << 20)) {
        if (pd && pd_level >= 0) xx_pd_leave_level(pd, pd_level);
        return false;
    }

    if (!ppmd8_rd_init(&ppmd, rd, rd->dev, rd->mem, rd->mem_size, rd->remaining)) {
        Ppmd8_Free(&ppmd);
        if (pd && pd_level >= 0) xx_pd_leave_level(pd, pd_level);
        return false;
    }

    Ppmd8_Init(&ppmd, (unsigned)order, (unsigned)restore_method);

    size_t opos = 0, tot = 0;
    uint8_t outbuf[65536];
    bool ok = true;

    while (ok) {
        if (pd && xx_pd_is_stopped(pd)) { ok = false; break; }

        if (uncomp_size >= 0 && (int64_t)tot + (int64_t)opos >= uncomp_size) {
            break;
        }

        int sym = Ppmd8_DecodeSymbol(&ppmd, rd);
        if (rd->error || sym < -1) {
            ok = false;
            break;
        }
        if (sym == -1) {
            /* End of stream */
            break;
        }

        outbuf[opos++] = (uint8_t)sym;
        if (opos >= sizeof(outbuf)) {
            if (dst_dev) {
                ssize_t w = xx_io_write(dst_dev, outbuf, opos);
                if (w < 0 || (size_t)w != opos) { ok = false; break; }
            } else if (mem_dst) {
                if (tot + opos > mem_cap) { ok = false; break; }
                xx_rt_memcpy(mem_dst + tot, outbuf, opos);
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
                xx_rt_memcpy(mem_dst + tot, outbuf, opos);
            else
                ok = false;
        }
        tot += opos;
        if (pd && pd_level >= 0) xx_pd_set_current(pd, pd_level, (uint64_t)tot);
    }

    if (out_written) *out_written = tot;
    Ppmd8_Free(&ppmd);

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    return ok;
}
