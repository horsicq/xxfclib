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

/* PPMd7 range encoder and streaming compressor.
 * Algorithm by Dmitry Shkarin (public domain, 2001).
 * Range coder adaptation by Igor Pavlov (public domain, 2017).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_ppmd7_internal.h"
#include <string.h>

#define kTopValue (1u << 24)

void ppmd7_re_init(ppmd7_range_enc *rc, xx_io_device *dev, uint8_t *mem, size_t mem_cap)
{
    xx_rt_memset(rc, 0, sizeof(*rc));
    rc->dev = dev;
    rc->mem = mem;
    rc->mem_cap = mem_cap;
    rc->low = 0;
    rc->range = 0xFFFFFFFFu;
    rc->cache = 0;
    rc->cache_size = 1;
}

static void re_write_byte(ppmd7_range_enc *rc, uint8_t b)
{
    if (rc->error) return;
    rc->obuf[rc->obuf_pos++] = b;
    if (rc->obuf_pos >= sizeof(rc->obuf)) {
        if (rc->dev) {
            ssize_t w = xx_io_write(rc->dev, rc->obuf, rc->obuf_pos);
            if (w < 0 || (size_t)w != rc->obuf_pos) {
                rc->error = true;
                return;
            }
        } else if (rc->mem) {
            if (rc->total_written + rc->obuf_pos > rc->mem_cap) {
                rc->error = true;
                return;
            }
            xx_rt_memcpy(rc->mem + rc->total_written, rc->obuf, rc->obuf_pos);
        }
        rc->total_written += rc->obuf_pos;
        rc->obuf_pos = 0;
    }
}

static void re_shift_low(ppmd7_range_enc *rc)
{
    if ((uint32_t)rc->low < 0xFF000000u || (unsigned)(rc->low >> 32) != 0) {
        uint8_t temp = rc->cache;
        do {
            re_write_byte(rc, (uint8_t)(temp + (uint8_t)(rc->low >> 32)));
            temp = 0xFF;
        } while (--rc->cache_size != 0);
        rc->cache = (uint8_t)((uint32_t)rc->low >> 24);
    }
    rc->cache_size++;
    rc->low = (uint32_t)rc->low << 8;
}

static void re_encode(ppmd7_range_enc *rc, uint32_t start, uint32_t size, uint32_t total)
{
    rc->low += start * (rc->range /= total);
    rc->range *= size;
    while (rc->range < kTopValue) {
        rc->range <<= 8;
        re_shift_low(rc);
    }
}

static void re_encode_bit_0(ppmd7_range_enc *rc, uint32_t size0)
{
    rc->range = (rc->range >> 14) * size0;
    while (rc->range < kTopValue) {
        rc->range <<= 8;
        re_shift_low(rc);
    }
}

static void re_encode_bit_1(ppmd7_range_enc *rc, uint32_t size0)
{
    uint32_t newBound = (rc->range >> 14) * size0;
    rc->low += newBound;
    rc->range -= newBound;
    while (rc->range < kTopValue) {
        rc->range <<= 8;
        re_shift_low(rc);
    }
}

void ppmd7_re_flush(ppmd7_range_enc *rc)
{
    for (unsigned i = 0; i < 5; i++)
        re_shift_low(rc);

    if (!rc->error && rc->obuf_pos > 0) {
        if (rc->dev) {
            ssize_t w = xx_io_write(rc->dev, rc->obuf, rc->obuf_pos);
            if (w < 0 || (size_t)w != rc->obuf_pos)
                rc->error = true;
        } else if (rc->mem) {
            if (rc->total_written + rc->obuf_pos <= rc->mem_cap)
                xx_rt_memcpy(rc->mem + rc->total_written, rc->obuf, rc->obuf_pos);
            else
                rc->error = true;
        }
        rc->total_written += rc->obuf_pos;
        rc->obuf_pos = 0;
    }
}

#define MASK(sym) ((int8_t *)charMask)[sym]

void Ppmd7_EncodeSymbol(CPpmd7 *p, ppmd7_range_enc *rc, int symbol)
{
    size_t charMask[256 / sizeof(size_t)];
    if (p->MinContext->NumStats != 1) {
        CPpmd_State *s = Ppmd7_GetStats(p, p->MinContext);
        uint32_t sum;
        unsigned i;
        if (s->Symbol == (uint8_t)symbol) {
            re_encode(rc, 0, s->Freq, p->MinContext->SummFreq);
            p->FoundState = s;
            Ppmd7_Update1_0(p);
            return;
        }
        p->PrevSuccess = 0;
        sum = s->Freq;
        i = p->MinContext->NumStats - 1;
        do {
            if ((++s)->Symbol == (uint8_t)symbol) {
                re_encode(rc, sum, s->Freq, p->MinContext->SummFreq);
                p->FoundState = s;
                Ppmd7_Update1(p);
                return;
            }
            sum += s->Freq;
        } while (--i);

        p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol];
        PPMD_SetAllBitsIn256Bytes(charMask);
        MASK(s->Symbol) = 0;
        i = p->MinContext->NumStats - 1;
        do { MASK((--s)->Symbol) = 0; } while (--i);
        re_encode(rc, sum, p->MinContext->SummFreq - sum, p->MinContext->SummFreq);
    } else {
        uint16_t *prob = Ppmd7_GetBinSumm(p);
        CPpmd_State *s = Ppmd7Context_OneState(p->MinContext);
        if (s->Symbol == (uint8_t)symbol) {
            re_encode_bit_0(rc, *prob);
            *prob = (uint16_t)PPMD_UPDATE_PROB_0(*prob);
            p->FoundState = s;
            Ppmd7_UpdateBin(p);
            return;
        } else {
            re_encode_bit_1(rc, *prob);
            *prob = (uint16_t)PPMD_UPDATE_PROB_1(*prob);
            p->InitEsc = PPMD7_kExpEscape[*prob >> 10];
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
            p->MinContext = Ppmd7_GetContext(p, p->MinContext->Suffix);
        } while (p->MinContext->NumStats == numMasked);

        see = Ppmd7_MakeEscFreq(p, numMasked, &escFreq);
        s = Ppmd7_GetStats(p, p->MinContext);
        sum = 0;
        i = p->MinContext->NumStats;
        do {
            int cur = s->Symbol;
            if (cur == symbol) {
                uint32_t low = sum;
                CPpmd_State *s1 = s;
                do {
                    sum += (s->Freq & (int)(MASK(s->Symbol)));
                    s++;
                } while (--i);
                re_encode(rc, low, s1->Freq, sum + escFreq);
                Ppmd_See_Update(see);
                p->FoundState = s1;
                Ppmd7_Update2(p);
                return;
            }
            sum += (s->Freq & (int)(MASK(cur)));
            MASK(cur) = 0;
            s++;
        } while (--i);

        re_encode(rc, sum, escFreq, sum + escFreq);
        see->Summ = (uint16_t)(see->Summ + sum + escFreq);
    }
}

bool xx_ppmd7_compress_stream(xx_io_device *src_dev, const uint8_t *src_mem, size_t src_size,
                              int64_t src_offset, int64_t uncomp_size,
                              xx_io_device *dst_dev, uint8_t *dst_mem, size_t dst_cap,
                              size_t *out_written, int order, uint32_t mem_mb, xx_pd_struct *pd)
{
    if (order < PPMD7_MIN_ORDER || order > PPMD7_MAX_ORDER) return false;
    if (mem_mb < 1) mem_mb = 1;
    if (mem_mb > XX_PPMD7_MAX_MEM_MB) mem_mb = XX_PPMD7_MAX_MEM_MB;
    if (!dst_dev && (!dst_mem && dst_cap > 0)) return false;
    if (!src_dev && !src_mem) return false;

    if (src_dev && src_offset > 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0)
            return false;
    }

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, uncomp_size > 0 ? (uint64_t)uncomp_size : 0, "Compressing PPMd7");
    }

    CPpmd7 ppmd;
    Ppmd7_Construct(&ppmd);
    if (!Ppmd7_Alloc(&ppmd, mem_mb * 1024u * 1024u)) {
        if (pd && pd_level >= 0) xx_pd_leave_level(pd, pd_level);
        return false;
    }
    Ppmd7_Init(&ppmd, (unsigned)order);

    ppmd7_range_enc rc;
    ppmd7_re_init(&rc, dst_dev, dst_mem, dst_cap);

    uint8_t in_buf[65536];
    int64_t rem = uncomp_size;
    size_t mem_pos = 0;
    bool ok = true;

    while (ok) {
        if (pd && xx_pd_is_stopped(pd)) { ok = false; break; }

        size_t want = sizeof(in_buf);
        if (uncomp_size >= 0) {
            if (rem <= 0) break;
            if ((int64_t)want > rem) want = (size_t)rem;
        }

        size_t got = 0;
        if (src_dev) {
            ssize_t r = xx_io_read(src_dev, in_buf, want);
            if (r < 0) { ok = false; break; }
            if (r == 0) break;
            got = (size_t)r;
        } else {
            size_t avail = src_size - mem_pos;
            if (avail == 0) break;
            got = avail < want ? avail : want;
            xx_rt_memcpy(in_buf, src_mem + mem_pos, got);
            mem_pos += got;
        }

        if (uncomp_size >= 0) rem -= (int64_t)got;

        for (size_t i = 0; i < got; i++) {
            Ppmd7_EncodeSymbol(&ppmd, &rc, (int)in_buf[i]);
            if (rc.error) { ok = false; break; }
        }

        if (pd && pd_level >= 0 && uncomp_size > 0) {
            int64_t done = uncomp_size - rem;
            xx_pd_set_current(pd, pd_level, (uint64_t)done);
        }
    }

    if (ok) {
        /* Write EndMarker and flush */
        Ppmd7_EncodeSymbol(&ppmd, &rc, -1);
        ppmd7_re_flush(&rc);
        if (rc.error) ok = false;
    }

    Ppmd7_Free(&ppmd);

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    if (ok && out_written)
        *out_written = rc.total_written;

    return ok;
}
