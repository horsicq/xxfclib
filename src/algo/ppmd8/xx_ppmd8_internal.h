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

/* Internal header for the xx_ppmd8 implementation. Not part of the public API.
 *
 * Implements PPMd variant H / PPMdI — used by PKWARE / 7-Zip ZIP method 98.
 * Algorithm by Dmitry Shkarin (public domain, 2002).
 * Carryless 32-bit rangecoder by Dmitry Subbotin (public domain, 1999).
 * Range coder adaptations by Igor Pavlov (public domain, 2018).
 */

#ifndef XX_PPMD8_INTERNAL_H
#define XX_PPMD8_INTERNAL_H

#include "xxfclib/algo/ppmd8/xx_ppmd8.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PPMD8_MIN_ORDER 2
#define PPMD8_MAX_ORDER 16

#define PPMD_INT_BITS 7
#define PPMD_PERIOD_BITS 7
#define PPMD_BIN_SCALE (1 << (PPMD_INT_BITS + PPMD_PERIOD_BITS))

#define PPMD_GET_MEAN_SPEC(summ, shift, round) (((summ) + (1 << ((shift) - (round)))) >> (shift))
#define PPMD_GET_MEAN(summ) PPMD_GET_MEAN_SPEC((summ), PPMD_PERIOD_BITS, 2)
#define PPMD_UPDATE_PROB_0(prob) ((prob) + (1 << PPMD_INT_BITS) - PPMD_GET_MEAN(prob))
#define PPMD_UPDATE_PROB_1(prob) ((prob) - PPMD_GET_MEAN(prob))

#define PPMD_N1 4
#define PPMD_N2 4
#define PPMD_N3 4
#define PPMD_N4 ((128 + 3 - 1 * PPMD_N1 - 2 * PPMD_N2 - 3 * PPMD_N3) / 4)
#define PPMD_NUM_INDEXES (PPMD_N1 + PPMD_N2 + PPMD_N3 + PPMD_N4)

#define PPMD8_UNIT_SIZE 12
#define PPMD8_MAX_FREQ  124

#pragma pack(push, 1)

typedef struct {
    uint16_t Summ;
    uint8_t  Shift;
    uint8_t  Count;
} CPpmd_See;

#define Ppmd_See_Update(p) if ((p)->Shift < PPMD_PERIOD_BITS && --(p)->Count == 0) \
    { (p)->Summ = (uint16_t)((p)->Summ << 1); (p)->Count = (uint8_t)(3 << (p)->Shift++); }

typedef struct {
    uint8_t  Symbol;
    uint8_t  Freq;
    uint16_t SuccessorLow;
    uint16_t SuccessorHigh;
} CPpmd_State;

#pragma pack(pop)

typedef uint32_t CPpmd_State_Ref;
typedef uint32_t CPpmd_Void_Ref;
typedef uint32_t CPpmd_Byte_Ref;
typedef uint32_t CPpmd8_Context_Ref;

#pragma pack(push, 1)

typedef struct CPpmd8_Context_ {
    uint8_t            NumStats;
    uint8_t            Flags;
    uint16_t           SummFreq;
    CPpmd_State_Ref    Stats;
    CPpmd8_Context_Ref Suffix;
} CPpmd8_Context;

#pragma pack(pop)

#define Ppmd8Context_OneState(p) ((CPpmd_State *)&(p)->SummFreq)

typedef struct {
    CPpmd8_Context *MinContext;
    CPpmd8_Context *MaxContext;
    CPpmd_State    *FoundState;
    unsigned        OrderFall;
    unsigned        InitEsc;
    unsigned        PrevSuccess;
    unsigned        MaxOrder;
    int32_t         RunLength;
    int32_t         InitRL;

    uint32_t        Size;
    uint32_t        GlueCount;
    uint8_t        *Base;
    uint8_t        *LoUnit;
    uint8_t        *HiUnit;
    uint8_t        *Text;
    uint8_t        *UnitsStart;
    uint32_t        AlignOffset;
    unsigned        RestoreMethod;

    /* Subbotin Range Coder state */
    uint32_t        Range;
    uint32_t        Code;
    uint32_t        Low;

    uint8_t         Indx2Units[PPMD_NUM_INDEXES];
    uint8_t         Units2Indx[128];
    CPpmd_Void_Ref  FreeList[PPMD_NUM_INDEXES];
    uint32_t        Stamps[PPMD_NUM_INDEXES];

    uint8_t         NS2BSIndx[256];
    uint8_t         NS2Indx[260];
    CPpmd_See       DummySee;
    CPpmd_See       See[24][32];
    uint16_t        BinSumm[25][64];
} CPpmd8;

#define PPMD_SetAllBitsIn256Bytes(p) \
    { size_t z; for (z = 0; z < 256 / sizeof((p)[0]); z += 8) { \
    (p)[z+7] = (p)[z+6] = (p)[z+5] = (p)[z+4] = (p)[z+3] = (p)[z+2] = (p)[z+1] = (p)[z+0] = ~(size_t)0; }}

#define Ppmd8_GetPtr(p, offs)     ((void *)((p)->Base + (offs)))
#define Ppmd8_GetContext(p, offs) ((CPpmd8_Context *)Ppmd8_GetPtr((p), (offs)))
#define Ppmd8_GetStats(p, ctx)    ((CPpmd_State *)Ppmd8_GetPtr((p), ((ctx)->Stats)))

#define Ppmd8_GetBinSumm(p) \
    (&p->BinSumm[p->NS2Indx[(size_t)Ppmd8Context_OneState(p->MinContext)->Freq - 1]][ \
    p->NS2BSIndx[Ppmd8_GetContext(p, p->MinContext->Suffix)->NumStats] + \
    p->PrevSuccess + p->MinContext->Flags + ((p->RunLength >> 26) & 0x20)])

extern const uint8_t PPMD8_kExpEscape[16];

void Ppmd8_Construct(CPpmd8 *p);
bool Ppmd8_Alloc(CPpmd8 *p, uint32_t size);
void Ppmd8_Free(CPpmd8 *p);
void Ppmd8_Init(CPpmd8 *p, unsigned maxOrder, unsigned restoreMethod);

void Ppmd8_Update1(CPpmd8 *p);
void Ppmd8_Update1_0(CPpmd8 *p);
void Ppmd8_Update2(CPpmd8 *p);
void Ppmd8_UpdateBin(CPpmd8 *p);

CPpmd_See *Ppmd8_MakeEscFreq(CPpmd8 *p, unsigned numMasked, uint32_t *scale);

/* ========================================================================= */
/* --- Range Decoder & Streaming Decompressor                             --- */
/* ========================================================================= */

typedef struct {
    xx_io_device  *dev;
    const uint8_t *mem;
    size_t         mem_size;
    size_t         mem_pos;
    int64_t        remaining;
    uint8_t        ibuf[65536];
    size_t         ibuf_pos;
    size_t         ibuf_len;
    bool           error;
} ppmd8_range_dec;

bool ppmd8_rd_init(CPpmd8 *p, ppmd8_range_dec *rd, xx_io_device *dev,
                   const uint8_t *mem, size_t mem_size, int64_t remaining);
int  Ppmd8_DecodeSymbol(CPpmd8 *p, ppmd8_range_dec *rd);

bool xx_ppmd8_decompress_stream(ppmd8_range_dec *rd,
                                int64_t uncomp_size,
                                int order, uint32_t mem_mb, int restore_method,
                                xx_io_device *dst_dev,
                                uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd);

/* ========================================================================= */
/* --- Range Encoder & Streaming Compressor                               --- */
/* ========================================================================= */

typedef struct {
    xx_io_device *dev;
    uint8_t      *mem;
    size_t        mem_cap;
    size_t        mem_pos;
    uint8_t       obuf[65536];
    size_t        obuf_pos;
    int64_t       total_written;
    bool          error;
} ppmd8_range_enc;

void ppmd8_re_init(CPpmd8 *p, ppmd8_range_enc *re, xx_io_device *dev,
                   uint8_t *mem, size_t mem_cap);
void Ppmd8_EncodeSymbol(CPpmd8 *p, ppmd8_range_enc *re, int symbol);
void Ppmd8_RangeEnc_FlushData(CPpmd8 *p, ppmd8_range_enc *re);
bool ppmd8_re_flush_buffer(ppmd8_range_enc *re);

bool xx_ppmd8_pack_stream(ppmd8_range_enc *re,
                          xx_io_device *src_dev,
                          const uint8_t *src_mem, size_t src_size,
                          int64_t uncomp_size,
                          int order, uint32_t mem_mb, int restore_method,
                          bool write_zip_header,
                          xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_PPMD8_INTERNAL_H */
