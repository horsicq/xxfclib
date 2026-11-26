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

/* Internal header for the xx_ppmd implementation. Not part of the public API.
 *
 * Implements PPMd variant I rev.1 (PPMd7) — the variant used by ZIP method 98.
 * Algorithm by Dmitry Shkarin (public domain, 1997-2006).
 * Range coder adaptations by Igor Pavlov (public domain).
 */

#ifndef XX_PPMD7_INTERNAL_H
#define XX_PPMD7_INTERNAL_H

#include "xxfclib/algo/ppmd7/xx_ppmd7.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h> /* _mm_prefetch */
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PPMD7_MIN_ORDER 2
#define PPMD7_MAX_ORDER 64

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

#define PPMD7_UNIT_SIZE 12
#define PPMD7_MAX_FREQ 124

#pragma pack(push, 1)

typedef struct {
    uint16_t Summ;
    uint8_t Shift;
    uint8_t Count;
} CPpmd_See;

#define Ppmd_See_Update(p)                                    \
    if ((p)->Shift < PPMD_PERIOD_BITS && --(p)->Count == 0) { \
        (p)->Summ = (uint16_t)((p)->Summ << 1);               \
        (p)->Count = (uint8_t)(3 << (p)->Shift++);            \
    }

typedef struct {
    uint8_t Symbol;
    uint8_t Freq;
    uint16_t SuccessorLow;
    uint16_t SuccessorHigh;
} CPpmd_State;

/* Unaligned 32-bit access (used for the successor field of a state). */
typedef struct {
    uint32_t v;
} CPpmd_U32;

#pragma pack(pop)

typedef uint32_t CPpmd_State_Ref;
typedef uint32_t CPpmd_Void_Ref;
typedef uint32_t CPpmd_Byte_Ref;
typedef uint32_t CPpmd7_Context_Ref;

typedef struct CPpmd7_Context_ {
    uint16_t NumStats;
    uint16_t SummFreq;
    CPpmd_State_Ref Stats;
    CPpmd7_Context_Ref Suffix;
} CPpmd7_Context;

#define Ppmd7Context_OneState(p) ((CPpmd_State *)&(p)->SummFreq)

typedef struct {
    CPpmd7_Context *MinContext;
    CPpmd7_Context *MaxContext;
    CPpmd_State *FoundState;
    unsigned OrderFall;
    unsigned InitEsc;
    unsigned PrevSuccess;
    unsigned MaxOrder;
    unsigned HiBitsFlag;
    int32_t RunLength;
    int32_t InitRL;

    uint32_t Size;
    uint32_t GlueCount;
    uint8_t *Base;
    uint8_t *LoUnit;
    uint8_t *HiUnit;
    uint8_t *Text;
    uint8_t *UnitsStart;
    uint32_t AlignOffset;

    uint8_t Indx2Units[PPMD_NUM_INDEXES];
    uint8_t Units2Indx[128];
    CPpmd_Void_Ref FreeList[PPMD_NUM_INDEXES];
    uint8_t NS2Indx[256];
    uint8_t NS2BSIndx[256];
    uint8_t HB2Flag[256];
    CPpmd_See DummySee;
    CPpmd_See See[25][16];
    uint16_t BinSumm[128][64];
    unsigned EscSimd; /* decoder only: 1 = the SSSE3 escape-sum kernel may be used */
} CPpmd7;

extern const uint8_t PPMD7_kExpEscape[16];

#define PPMD_SetAllBitsIn256Bytes(p)                                                                                                    \
    do {                                                                                                                                \
        size_t _z;                                                                                                                      \
        for (_z = 0; _z < 256 / sizeof((p)[0]); _z += 8) {                                                                              \
            (p)[_z + 7] = (p)[_z + 6] = (p)[_z + 5] = (p)[_z + 4] = (p)[_z + 3] = (p)[_z + 2] = (p)[_z + 1] = (p)[_z + 0] = ~(size_t)0; \
        }                                                                                                                               \
    } while (0)

#define Ppmd7_GetPtr(p, offs) ((void *)((p)->Base + (offs)))
#define Ppmd7_GetContext(p, offs) ((CPpmd7_Context *)Ppmd7_GetPtr((p), (offs)))
#define Ppmd7_GetStats(p, ctx) ((CPpmd_State *)Ppmd7_GetPtr((p), ((ctx)->Stats)))
#define Ppmd7_GetSuccessor(p) ppmd7_get_successor(p)

/* Software prefetch (a hint only: it never faults and never changes results). */
#if defined(PPMD7_NO_PREFETCH)
#define PPMD7_PREFETCH(a) ((void)(a))
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define PPMD7_PREFETCH(a) _mm_prefetch((const char *)(a), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
#define PPMD7_PREFETCH(a) __builtin_prefetch((const void *)(a))
#else
#define PPMD7_PREFETCH(a) ((void)(a))
#endif

/* The successor reference is the little-endian 32-bit value at byte offset 2
 * of the packed 6-byte state; MSVC on little-endian x86 reads/writes it in one
 * unaligned access (other compilers keep the two 16-bit halves, which they
 * merge themselves where that is legal). */
#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_IX86))
#define PPMD7_LE_UNALIGNED 1
#endif

static inline CPpmd_Void_Ref ppmd7_get_successor(const CPpmd_State *s)
{
#ifdef PPMD7_LE_UNALIGNED
    return ((const CPpmd_U32 *)(const void *)&s->SuccessorLow)->v;
#else
    return (CPpmd_Void_Ref)(s->SuccessorLow | ((uint32_t)s->SuccessorHigh << 16));
#endif
}

static inline void ppmd7_set_successor(CPpmd_State *s, CPpmd_Void_Ref v)
{
#ifdef PPMD7_LE_UNALIGNED
    ((CPpmd_U32 *)(void *)&s->SuccessorLow)->v = v;
#else
    s->SuccessorLow = (uint16_t)(v & 0xFFFF);
    s->SuccessorHigh = (uint16_t)((v >> 16) & 0xFFFF);
#endif
}

#define Ppmd7_GetBinSumm(p)                                                                                                                                             \
    &p->BinSumm[(size_t)(unsigned)Ppmd7Context_OneState(p->MinContext)->Freq - 1]                                                                                       \
               [p->PrevSuccess + p->NS2BSIndx[(size_t)Ppmd7_GetContext(p, p->MinContext->Suffix)->NumStats - 1] + (p->HiBitsFlag = p->HB2Flag[p->FoundState->Symbol]) + \
                2 * p->HB2Flag[(unsigned)Ppmd7Context_OneState(p->MinContext)->Symbol] + ((p->RunLength >> 26) & 0x20)]

void Ppmd7_Construct(CPpmd7 *p);
bool Ppmd7_Alloc(CPpmd7 *p, uint32_t size);
void Ppmd7_Free(CPpmd7 *p);
void Ppmd7_Init(CPpmd7 *p, unsigned maxOrder);

void Ppmd7_Update1(CPpmd7 *p);
void Ppmd7_Update1_0(CPpmd7 *p);
void Ppmd7_Update2(CPpmd7 *p);
void Ppmd7_UpdateBin(CPpmd7 *p);
CPpmd_See *Ppmd7_MakeEscFreq(CPpmd7 *p, unsigned numMasked, uint32_t *scale);

void xx_ppmd7_update_model(CPpmd7 *p);
void xx_ppmd7_rescale(CPpmd7 *p);

static inline void ppmd7_swap_states(CPpmd_State *t1, CPpmd_State *t2)
{
    CPpmd_State tmp = *t1;
    *t1 = *t2;
    *t2 = tmp;
}

static inline void ppmd7_next_context(CPpmd7 *p)
{
    CPpmd7_Context *c = Ppmd7_GetContext(p, Ppmd7_GetSuccessor(p->FoundState));
    if (p->OrderFall == 0 && (uint8_t *)c > p->Text) p->MinContext = p->MaxContext = c;
    else xx_ppmd7_update_model(p);
}

static inline void ppmd7_update1(CPpmd7 *p)
{
    CPpmd_State *s = p->FoundState;
    s->Freq += 4;
    p->MinContext->SummFreq += 4;
    if (s[0].Freq > s[-1].Freq) {
        ppmd7_swap_states(&s[0], &s[-1]);
        p->FoundState = --s;
        if (s->Freq > PPMD7_MAX_FREQ) xx_ppmd7_rescale(p);
    }
    ppmd7_next_context(p);
}

static inline void ppmd7_update1_0(CPpmd7 *p)
{
    p->PrevSuccess = (2 * p->FoundState->Freq > p->MinContext->SummFreq);
    p->RunLength += p->PrevSuccess;
    p->MinContext->SummFreq += 4;
    if ((p->FoundState->Freq += 4) > PPMD7_MAX_FREQ) xx_ppmd7_rescale(p);
    ppmd7_next_context(p);
}

static inline void ppmd7_update_bin(CPpmd7 *p)
{
    p->FoundState->Freq = (uint8_t)(p->FoundState->Freq + (p->FoundState->Freq < 128 ? 1 : 0));
    p->PrevSuccess = 1;
    p->RunLength++;
    ppmd7_next_context(p);
}

static inline void ppmd7_update2(CPpmd7 *p)
{
    p->MinContext->SummFreq += 4;
    if ((p->FoundState->Freq += 4) > PPMD7_MAX_FREQ) xx_ppmd7_rescale(p);
    p->RunLength = p->InitRL;
    xx_ppmd7_update_model(p);
}

static inline CPpmd_See *ppmd7_make_esc_freq(CPpmd7 *p, unsigned numMasked, uint32_t *escFreq)
{
    CPpmd_See *see;
    unsigned nonMasked = p->MinContext->NumStats - numMasked;
    if (p->MinContext->NumStats != 256) {
        see = p->See[(unsigned)p->NS2Indx[(size_t)nonMasked - 1]] +
              (nonMasked < (unsigned)Ppmd7_GetContext(p, p->MinContext->Suffix)->NumStats - p->MinContext->NumStats) +
              2 * (unsigned)(p->MinContext->SummFreq < 11 * p->MinContext->NumStats) + 4 * (unsigned)(numMasked > nonMasked) + p->HiBitsFlag;
        {
            unsigned r = (see->Summ >> see->Shift);
            see->Summ = (uint16_t)(see->Summ - r);
            *escFreq = r + (r == 0);
        }
    } else {
        see = &p->DummySee;
        *escFreq = 1;
    }
    return see;
}

#define Ppmd7_Update1(p) ppmd7_update1(p)
#define Ppmd7_Update1_0(p) ppmd7_update1_0(p)
#define Ppmd7_Update2(p) ppmd7_update2(p)
#define Ppmd7_UpdateBin(p) ppmd7_update_bin(p)
#define Ppmd7_MakeEscFreq(p, numMasked, escFreq) ppmd7_make_esc_freq((p), (numMasked), (escFreq))

/* =========================================================================
 * Range decoder
 * ========================================================================= */
typedef struct {
    xx_io_device *dev;
    const uint8_t *mem;
    size_t mem_size;
    size_t mem_pos;
    uint8_t *ibuf;
    size_t ibuf_capacity, io_capacity;
    uint8_t input_byte; /* scalar startup/low-level coder state */
    size_t ibuf_pos;
    size_t ibuf_len;
    int64_t remaining;
    uint32_t range;
    uint32_t code;
    bool error;
} ppmd7_range_dec;

bool ppmd7_rd_init(ppmd7_range_dec *rd, xx_io_device *dev, const uint8_t *mem, size_t mem_size, int64_t remaining);
int Ppmd7_DecodeSymbol(CPpmd7 *p, ppmd7_range_dec *rc);

bool xx_ppmd7_decompress_stream(ppmd7_range_dec *rd, int order, uint32_t mem_size, xx_io_device *dst_dev, uint8_t *mem_dst, size_t mem_cap, size_t *out_written,
                                xx_pd_struct *pd);
/* Sized 7z streams need not contain a PPMd EOS symbol. */
bool xx_ppmd7_decompress_stream_sized(ppmd7_range_dec *rd, int order, uint32_t mem_size, xx_io_device *destination, uint64_t expected_size, xx_pd_struct *pd);

/* =========================================================================
 * Range encoder
 * ========================================================================= */
typedef struct {
    xx_io_device *dev;
    uint8_t *mem;
    size_t mem_cap;
    size_t mem_pos;
    uint8_t *obuf;
    size_t obuf_capacity, io_capacity;
    uint8_t output_byte; /* scalar low-level coder state */
    size_t obuf_pos;
    size_t total_written;
    uint64_t low;
    uint32_t range;
    uint64_t cache_size;
    uint8_t cache;
    bool error;
} ppmd7_range_enc;

void ppmd7_re_init(ppmd7_range_enc *rc, xx_io_device *dev, uint8_t *mem, size_t mem_cap);
void ppmd7_re_flush(ppmd7_range_enc *rc);
void Ppmd7_EncodeSymbol(CPpmd7 *p, ppmd7_range_enc *rc, int symbol);

bool xx_ppmd7_compress_stream(xx_io_device *src_dev, const uint8_t *src_mem, size_t src_size, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev,
                              uint8_t *dst_mem, size_t dst_cap, size_t *out_written, int order, uint32_t mem_mb, xx_pd_struct *pd);

/* Known-size 7z folder framing: flush the range coder without an EOS symbol. */
bool xx_ppmd7_compress_stream_sized(xx_io_device *src_dev, const uint8_t *src_mem, size_t src_size, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev,
                                    uint8_t *dst_mem, size_t dst_cap, size_t *out_written, int order, uint32_t mem_mb, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_PPMD7_INTERNAL_H */
