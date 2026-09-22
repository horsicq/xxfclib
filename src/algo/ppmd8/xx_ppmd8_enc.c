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

/* PPMd8 range encoder and streaming compressor.
 * Algorithm by Dmitry Shkarin (public domain, 2002).
 * Carryless 32-bit rangecoder by Dmitry Subbotin (public domain, 1999).
 * Range coder adaptation by Igor Pavlov (public domain, 2017).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_ppmd8_internal.h"
#include <string.h>

#define kTop (1u << 24)
#define kBot (1u << 15)

static inline void ppmd8_re_byte(ppmd8_range_enc *re, uint8_t b)
{
    re->obuf[re->obuf_pos++] = b;
    if (re->obuf_pos >= sizeof(re->obuf)) {
        ppmd8_re_flush_buffer(re);
    }
}

bool ppmd8_re_flush_buffer(ppmd8_range_enc *re)
{
    if (re->error || re->obuf_pos == 0) return !re->error;
    if (re->dev) {
        ssize_t w = xx_io_write(re->dev, re->obuf, re->obuf_pos);
        if (w < 0 || (size_t)w != re->obuf_pos) {
            re->error = true;
            return false;
        }
    } else if (re->mem) {
        if (re->mem_pos + re->obuf_pos > re->mem_cap) {
            re->error = true;
            return false;
        }
        xx_rt_memcpy(re->mem + re->mem_pos, re->obuf, re->obuf_pos);
        re->mem_pos += re->obuf_pos;
    }
    re->total_written += (int64_t)re->obuf_pos;
    re->obuf_pos = 0;
    return true;
}

void ppmd8_re_init(CPpmd8 *p, ppmd8_range_enc *re, xx_io_device *dev,
                   uint8_t *mem, size_t mem_cap)
{
    xx_rt_memset(re, 0, sizeof(*re));
    re->dev = dev;
    re->mem = mem;
    re->mem_cap = mem_cap;
    if (p) {
        p->Low = 0;
        p->Range = 0xFFFFFFFFu;
    }
}

void Ppmd8_RangeEnc_FlushData(CPpmd8 *p, ppmd8_range_enc *re)
{
    for (int i = 0; i < 4; i++, p->Low <<= 8)
        ppmd8_re_byte(re, (uint8_t)(p->Low >> 24));
}

static void RangeEnc_Normalize(CPpmd8 *p, ppmd8_range_enc *re)
{
    while ((p->Low ^ (p->Low + p->Range)) < kTop ||
           (p->Range < kBot && ((p->Range = (0 - p->Low) & (kBot - 1)), 1))) {
        ppmd8_re_byte(re, (uint8_t)(p->Low >> 24));
        p->Range <<= 8;
        p->Low <<= 8;
    }
}

static void RangeEnc_Encode(CPpmd8 *p, ppmd8_range_enc *re, uint32_t start, uint32_t size, uint32_t total)
{
    p->Low += start * (p->Range /= total);
    p->Range *= size;
    RangeEnc_Normalize(p, re);
}

static void RangeEnc_EncodeBit_0(CPpmd8 *p, ppmd8_range_enc *re, uint32_t size0)
{
    p->Range >>= 14;
    p->Range *= size0;
    RangeEnc_Normalize(p, re);
}

static void RangeEnc_EncodeBit_1(CPpmd8 *p, ppmd8_range_enc *re, uint32_t size0)
{
    p->Low += size0 * (p->Range >>= 14);
    p->Range *= ((1 << 14) - size0);
    RangeEnc_Normalize(p, re);
}

#define MASK(sym) ((int8_t *)charMask)[sym]

void Ppmd8_EncodeSymbol(CPpmd8 *p, ppmd8_range_enc *re, int symbol)
{
    size_t charMask[256 / sizeof(size_t)];
    if (p->MinContext->NumStats != 0) {
        CPpmd_State *s = Ppmd8_GetStats(p, p->MinContext);
        uint32_t sum;
        unsigned i;
        if (s->Symbol == symbol) {
            RangeEnc_Encode(p, re, 0, s->Freq, p->MinContext->SummFreq);
            p->FoundState = s;
            Ppmd8_Update1_0(p);
            return;
        }
        p->PrevSuccess = 0;
        sum = s->Freq;
        i = p->MinContext->NumStats;
        do {
            if ((++s)->Symbol == symbol) {
                RangeEnc_Encode(p, re, sum, s->Freq, p->MinContext->SummFreq);
                p->FoundState = s;
                Ppmd8_Update1(p);
                return;
            }
            sum += s->Freq;
        } while (--i);

        PPMD_SetAllBitsIn256Bytes(charMask);
        MASK(s->Symbol) = 0;
        i = p->MinContext->NumStats;
        do {
            MASK((--s)->Symbol) = 0;
        } while (--i);
        RangeEnc_Encode(p, re, sum, p->MinContext->SummFreq - sum, p->MinContext->SummFreq);
    } else {
        uint16_t *prob = Ppmd8_GetBinSumm(p);
        CPpmd_State *s = Ppmd8Context_OneState(p->MinContext);
        if (s->Symbol == symbol) {
            RangeEnc_EncodeBit_0(p, re, *prob);
            *prob = (uint16_t)PPMD_UPDATE_PROB_0(*prob);
            p->FoundState = s;
            Ppmd8_UpdateBin(p);
            return;
        } else {
            RangeEnc_EncodeBit_1(p, re, *prob);
            *prob = (uint16_t)PPMD_UPDATE_PROB_1(*prob);
            p->InitEsc = PPMD8_kExpEscape[*prob >> 10];
            PPMD_SetAllBitsIn256Bytes(charMask);
            MASK(s->Symbol) = 0;
            p->PrevSuccess = 0;
        }
    }

    for (;;) {
        uint32_t escFreq;
        CPpmd_See *see;
        CPpmd_State *s;
        uint32_t sum;
        unsigned i, numMasked = p->MinContext->NumStats;
        do {
            p->OrderFall++;
            if (!p->MinContext->Suffix)
                return; /* EndMarker (symbol = -1) */
            p->MinContext = Ppmd8_GetContext(p, p->MinContext->Suffix);
        } while (p->MinContext->NumStats == numMasked);

        see = Ppmd8_MakeEscFreq(p, numMasked, &escFreq);
        s = Ppmd8_GetStats(p, p->MinContext);
        sum = 0;
        i = p->MinContext->NumStats + 1;
        do {
            int cur = s->Symbol;
            if (cur == symbol) {
                uint32_t low = sum;
                CPpmd_State *s1 = s;
                do {
                    sum += (s->Freq & (uint32_t)(MASK(s->Symbol)));
                    s++;
                } while (--i);
                RangeEnc_Encode(p, re, low, s1->Freq, sum + escFreq);
                Ppmd_See_Update(see);
                p->FoundState = s1;
                Ppmd8_Update2(p);
                return;
            }
            sum += (s->Freq & (uint32_t)(MASK(cur)));
            MASK(cur) = 0;
            s++;
        } while (--i);

        RangeEnc_Encode(p, re, sum, escFreq, sum + escFreq);
        see->Summ = (uint16_t)(see->Summ + sum + escFreq);
    }
}

bool xx_ppmd8_pack_stream(ppmd8_range_enc *re,
                          xx_io_device *src_dev,
                          const uint8_t *src_mem, size_t src_size,
                          int64_t uncomp_size,
                          int order, uint32_t mem_mb, int restore_method,
                          bool write_zip_header,
                          xx_pd_struct *pd)
{
    if (order < PPMD8_MIN_ORDER || order > PPMD8_MAX_ORDER) return false;
    if (restore_method < 0 || restore_method > 1) return false;
    if (mem_mb < XX_PPMD8_MIN_MEM_MB) mem_mb = XX_PPMD8_MIN_MEM_MB;
    if (mem_mb > XX_PPMD8_MAX_MEM_MB) mem_mb = XX_PPMD8_MAX_MEM_MB;

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, uncomp_size > 0 ? (uint64_t)uncomp_size : 0, "Compressing PPMd8");
    }

    if (write_zip_header) {
        uint16_t hdr = xx_ppmd8_build_zip_props(order, mem_mb, restore_method);
        ppmd8_re_byte(re, (uint8_t)(hdr & 0xFF));
        ppmd8_re_byte(re, (uint8_t)(hdr >> 8));
        if (re->error) {
            if (pd && pd_level >= 0) xx_pd_leave_level(pd, pd_level);
            return false;
        }
    }

    CPpmd8 ppmd;
    Ppmd8_Construct(&ppmd);
    if (!Ppmd8_Alloc(&ppmd, mem_mb << 20)) {
        if (pd && pd_level >= 0) xx_pd_leave_level(pd, pd_level);
        return false;
    }

    ppmd.Low = 0;
    ppmd.Range = 0xFFFFFFFFu;
    Ppmd8_Init(&ppmd, (unsigned)order, (unsigned)restore_method);

    uint8_t inbuf[65536];
    int64_t in_processed = 0;
    size_t  mem_read_pos = 0;
    bool    ok = true;

    for (;;) {
        if (pd && xx_pd_is_stopped(pd)) { ok = false; break; }

        size_t to_read = sizeof(inbuf);
        if (uncomp_size >= 0) {
            int64_t rem = uncomp_size - in_processed;
            if (rem <= 0) break;
            if ((int64_t)to_read > rem) to_read = (size_t)rem;
        }

        size_t nread = 0;
        if (src_dev) {
            ssize_t r = xx_io_read(src_dev, inbuf, to_read);
            if (r < 0) { ok = false; break; }
            nread = (size_t)r;
        } else if (src_mem) {
            size_t rem = src_size > mem_read_pos ? (src_size - mem_read_pos) : 0;
            nread = rem < to_read ? rem : to_read;
            if (nread > 0) {
                xx_rt_memcpy(inbuf, src_mem + mem_read_pos, nread);
                mem_read_pos += nread;
            }
        }

        if (nread == 0) break;

        for (size_t i = 0; i < nread; i++) {
            Ppmd8_EncodeSymbol(&ppmd, re, (int)inbuf[i]);
            if (re->error) { ok = false; break; }
        }
        if (!ok) break;

        in_processed += (int64_t)nread;
        if (pd && pd_level >= 0) xx_pd_set_current(pd, pd_level, (uint64_t)in_processed);
    }

    if (ok) {
        Ppmd8_EncodeSymbol(&ppmd, re, -1);
        Ppmd8_RangeEnc_FlushData(&ppmd, re);
        if (!ppmd8_re_flush_buffer(re))
            ok = false;
    }

    Ppmd8_Free(&ppmd);

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    return ok && !re->error;
}
