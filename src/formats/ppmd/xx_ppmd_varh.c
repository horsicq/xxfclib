/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PPMd var.H stream decoder for the .pmd reader.
 *
 * PPMd var.H (Dmitry Shkarin, 2001, public domain) is the PPMd7 context
 * model of algo/ppmd7, driven by Dmitry Subbotin's carryless range coder
 * (1999, public domain) instead of the 7z range coder algo/ppmd7 uses.  The
 * symbol decoder below follows the structure of algo/ppmd7's
 * Ppmd7_DecodeSymbol (this library, MIT) with the coder operations replaced:
 *
 *   normalize: while ((low ^ (low + range)) < 2^24 ||
 *                     (range < 2^15 && (range = -low & (2^15 - 1), 1)))
 *                  code = code << 8 | next byte, range <<= 8, low <<= 8
 *   decode(start, size): low += start * range; code -= start * range;
 *                        range *= size; normalize
 *   threshold(total):    range /= total; code / range
 *   binary context:      size0 = (range >> 14) * prob;
 *                        bit 0: range = size0
 *                        bit 1: low += size0, code -= size0,
 *                               range = (range & ~(2^14 - 1)) - size0
 *
 * The stream starts with four code bytes (no leading zero byte, unlike the
 * 7z coder) and ends with an escape out of the order-0 context; a stream the
 * encoder flushed properly leaves `code` at 0.
 *
 * Hostile input: every threshold division is guarded (a total larger than
 * the range would make the next division divide by zero), a count past the
 * total is refused, and reads past the stream's window stop the decode.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_ppmd_codec.h"
#include "../../algo/ppmd7/xx_ppmd7_internal.h"

#define PPMDH_TOP ((uint32_t)1U << 24)
#define PPMDH_BOT ((uint32_t)1U << 15)

typedef struct ppmdh_coder_s {
    uint32_t low;
    uint32_t range;
    uint32_t code;
    xx_ppmdfile_source *source;
} ppmdh_coder;

static void ppmdh_normalize(ppmdh_coder *rc) {
    /* Each pass shifts one byte in; `range` is non-zero after the reset
     * (the distance to the next 2^15 boundary), so the loop ends after a
     * handful of passes. */
    while ((rc->low ^ (rc->low + rc->range)) < PPMDH_TOP ||
           (rc->range < PPMDH_BOT &&
            ((rc->range = (0U - rc->low) & (PPMDH_BOT - 1U)), 1))) {
        rc->code = (rc->code << 8) | xx_ppmdfile_source_byte(rc->source);
        rc->range <<= 8;
        rc->low <<= 8;
        if (rc->source->overrun || rc->source->io_error) return;
    }
}

static void ppmdh_decode(ppmdh_coder *rc, uint32_t start, uint32_t size) {
    start *= rc->range;
    rc->low += start;
    rc->code -= start;
    rc->range *= size;
    ppmdh_normalize(rc);
}

/* Returns false when `total` cannot be divided out of the range. */
static bool ppmdh_threshold(ppmdh_coder *rc, uint32_t total, uint32_t *count) {
    if (total == 0U || total > rc->range) return false;
    rc->range /= total;
    *count = rc->code / rc->range;
    return true;
}

#define PPMDH_MASK(sym) ((int8_t *)char_mask)[sym]

/* -1 end marker, 0..255 symbol, -2 data error. */
static int ppmdh_decode_symbol(CPpmd7 *p, ppmdh_coder *rc) {
    size_t char_mask[256 / sizeof(size_t)];
    if (p->MinContext->NumStats != 1) {
        CPpmd_State *s = Ppmd7_GetStats(p, p->MinContext);
        unsigned i;
        uint32_t count, hi_cnt;
        if (!ppmdh_threshold(rc, p->MinContext->SummFreq, &count)) return -2;
        if (count < (hi_cnt = s->Freq)) {
            uint8_t symbol;
            ppmdh_decode(rc, 0, s->Freq);
            p->FoundState = s;
            symbol = s->Symbol;
            Ppmd7_Update1_0(p);
            return (int)symbol;
        }
        p->PrevSuccess = 0;
        i = p->MinContext->NumStats - 1U;
        do {
            if ((hi_cnt += (++s)->Freq) > count) {
                uint8_t symbol;
                ppmdh_decode(rc, hi_cnt - s->Freq, s->Freq);
                p->FoundState = s;
                symbol = s->Symbol;
                Ppmd7_Update1(p);
                return (int)symbol;
            }
        } while (--i);
        if (count >= p->MinContext->SummFreq) return -2;
        p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol];
        ppmdh_decode(rc, hi_cnt, p->MinContext->SummFreq - hi_cnt);
        PPMD_SetAllBitsIn256Bytes(char_mask);
        PPMDH_MASK(s->Symbol) = 0;
        i = p->MinContext->NumStats - 1U;
        do {
            PPMDH_MASK((--s)->Symbol) = 0;
        } while (--i);
    } else {
        uint16_t *prob = Ppmd7_GetBinSumm(p);
        uint32_t size0 = (rc->range >> 14) * (uint32_t)*prob;
        if (rc->code < size0) {
            uint8_t symbol;
            rc->range = size0;
            ppmdh_normalize(rc);
            *prob = (uint16_t)PPMD_UPDATE_PROB_0(*prob);
            symbol = (p->FoundState = Ppmd7Context_OneState(p->MinContext))->Symbol;
            Ppmd7_UpdateBin(p);
            return (int)symbol;
        }
        rc->low += size0;
        rc->code -= size0;
        rc->range = (rc->range & ~((uint32_t)PPMD_BIN_SCALE - 1U)) - size0;
        ppmdh_normalize(rc);
        *prob = (uint16_t)PPMD_UPDATE_PROB_1(*prob);
        p->InitEsc = PPMD7_kExpEscape[*prob >> 10];
        PPMD_SetAllBitsIn256Bytes(char_mask);
        PPMDH_MASK(Ppmd7Context_OneState(p->MinContext)->Symbol) = 0;
        p->PrevSuccess = 0;
    }

    for (;;) {
        CPpmd_State *ps[256], *s;
        uint32_t freq_sum, count, hi_cnt;
        CPpmd_See *see;
        unsigned i, num, num_masked = p->MinContext->NumStats;
        if (rc->source->overrun || rc->source->io_error) return -2;
        do {
            p->OrderFall++;
            if (!p->MinContext->Suffix) return -1;
            p->MinContext = Ppmd7_GetContext(p, p->MinContext->Suffix);
        } while (p->MinContext->NumStats == num_masked);
        /* A suffix context always has more symbols than its child. */
        if (p->MinContext->NumStats < num_masked) return -2;

        hi_cnt = 0;
        s = Ppmd7_GetStats(p, p->MinContext);
        i = 0;
        num = p->MinContext->NumStats - num_masked;
        do {
            int k = (int)(PPMDH_MASK(s->Symbol));
            hi_cnt += (uint32_t)(s->Freq & k);
            ps[i] = s++;
            i -= (unsigned)k;
        } while (i != num);

        see = Ppmd7_MakeEscFreq(p, num_masked, &freq_sum);
        freq_sum += hi_cnt;
        if (!ppmdh_threshold(rc, freq_sum, &count)) return -2;

        if (count < hi_cnt) {
            uint8_t symbol;
            CPpmd_State **pps = ps;
            for (hi_cnt = 0; (hi_cnt += (*pps)->Freq) <= count; pps++) {
            }
            s = *pps;
            ppmdh_decode(rc, hi_cnt - s->Freq, s->Freq);
            Ppmd_See_Update(see);
            p->FoundState = s;
            symbol = s->Symbol;
            Ppmd7_Update2(p);
            return (int)symbol;
        }
        if (count >= freq_sum) return -2;
        ppmdh_decode(rc, hi_cnt, freq_sum - hi_cnt);
        see->Summ = (uint16_t)(see->Summ + freq_sum);
        do {
            PPMDH_MASK(ps[--i]->Symbol) = 0;
        } while (i != 0);
    }
}

xx_ppmdfile_status xx_ppmdfile_decode_varh(xx_ppmdfile_source *source,
                                           unsigned order, uint32_t mem_size,
                                           xx_ppmdfile_write_fn write,
                                           void *write_context,
                                           uint64_t max_output,
                                           uint64_t *out_size,
                                           xx_pd_struct *pd) {
    CPpmd7 *model;
    ppmdh_coder rc;
    uint8_t *out;
    size_t pending = 0U;
    uint64_t total = 0U;
    xx_ppmdfile_status status = XX_PPMDFILE_OK;
    unsigned index;
    if (out_size) *out_size = 0U;
    if (!source || order < PPMD7_MIN_ORDER || order > 16U ||
        mem_size < XX_PPMD7_MIN_MEM_SIZE || mem_size > XX_PPMD7_MAX_MEM_SIZE)
        return XX_PPMDFILE_BAD_PARAMS;
    model = (CPpmd7 *)xx_mem_alloc(sizeof(*model));
    out = (uint8_t *)xx_mem_alloc(XX_PPMDFILE_OUT_BUFFER);
    if (!model || !out) {
        if (model) xx_mem_free(model);
        if (out) xx_mem_free(out);
        return XX_PPMDFILE_NO_MEMORY;
    }
    Ppmd7_Construct(model);
    if (!Ppmd7_Alloc(model, mem_size)) {
        xx_mem_free(model);
        xx_mem_free(out);
        return XX_PPMDFILE_NO_MEMORY;
    }
    Ppmd7_Init(model, order);

    rc.source = source;
    rc.low = 0U;
    rc.range = 0xFFFFFFFFU;
    rc.code = 0U;
    for (index = 0U; index < 4U; ++index)
        rc.code = (rc.code << 8) | xx_ppmdfile_source_byte(source);
    if (source->io_error) status = XX_PPMDFILE_DATA_ERROR;
    else if (source->overrun) status = XX_PPMDFILE_TRUNCATED;
    else if (rc.code == 0xFFFFFFFFU) status = XX_PPMDFILE_DATA_ERROR;

    while (status == XX_PPMDFILE_OK) {
        int symbol = ppmdh_decode_symbol(model, &rc);
        if (source->io_error) {
            status = XX_PPMDFILE_DATA_ERROR;
            break;
        }
        if (source->overrun) {
            status = XX_PPMDFILE_TRUNCATED;
            break;
        }
        if (symbol == -1) {
            if (rc.code != 0U) status = XX_PPMDFILE_DATA_ERROR;
            break;
        }
        if (symbol < 0) {
            status = XX_PPMDFILE_DATA_ERROR;
            break;
        }
        if (max_output != 0U && total >= max_output) {
            status = XX_PPMDFILE_LIMIT;
            break;
        }
        out[pending++] = (uint8_t)symbol;
        ++total;
        if (pending == XX_PPMDFILE_OUT_BUFFER) {
            if (write && !write(write_context, out, pending)) {
                status = XX_PPMDFILE_WRITE_ERROR;
                break;
            }
            pending = 0U;
            if (pd && xx_pd_is_stopped(pd)) {
                status = XX_PPMDFILE_STOPPED;
                break;
            }
        }
    }
    if (status == XX_PPMDFILE_OK && pending != 0U && write &&
        !write(write_context, out, pending))
        status = XX_PPMDFILE_WRITE_ERROR;
    if (out_size) *out_size = total;
    Ppmd7_Free(model);
    xx_mem_free(model);
    xx_mem_free(out);
    return status;
}
