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

/* PPMd7 model and sub-allocator core.
 * Algorithm by Dmitry Shkarin (public domain, 2001).
 * Codec adaptation by Igor Pavlov (public domain, 2018).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_ppmd7_internal.h"
#include <string.h>

const uint8_t PPMD7_kExpEscape[16] = { 25, 14, 9, 7, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2 };
static const uint16_t kInitBinEsc[] = { 0x3CDD, 0x1F3F, 0x59BF, 0x48F3, 0x64A1, 0x5ABC, 0x6632, 0x6051 };

#define MAX_FREQ  PPMD7_MAX_FREQ
#define UNIT_SIZE PPMD7_UNIT_SIZE

#define U2B(nu) ((uint32_t)(nu) * UNIT_SIZE)
#define U2I(nu) (p->Units2Indx[(size_t)(nu) - 1])
#define I2U(indx) (p->Indx2Units[indx])

#define REF(ptr) ((uint32_t)((uint8_t *)(ptr) - (p)->Base))
#define STATS_REF(ptr) ((CPpmd_State_Ref)REF(ptr))

#define CTX(ref) ((CPpmd7_Context *)Ppmd7_GetContext(p, ref))
#define STATS(ctx) Ppmd7_GetStats(p, ctx)
#define ONE_STATE(ctx) Ppmd7Context_OneState(ctx)
#define SUFFIX(ctx) CTX((ctx)->Suffix)

typedef CPpmd7_Context * CTX_PTR;

typedef struct CPpmd7_Node_ {
    uint16_t Stamp;
    uint16_t NU;
    uint32_t Next;
    uint32_t Prev;
} CPpmd7_Node;

#define NODE(offs) ((CPpmd7_Node *)(p->Base + (offs)))

void Ppmd7_Construct(CPpmd7 *p)
{
    unsigned i, k, m;
    xx_rt_memset(p, 0, sizeof(*p));
    p->Base = NULL;

    for (i = 0, k = 0; i < PPMD_NUM_INDEXES; i++) {
        unsigned step = (i >= 12 ? 4 : (i >> 2) + 1);
        do { p->Units2Indx[k++] = (uint8_t)i; } while (--step);
        p->Indx2Units[i] = (uint8_t)k;
    }

    p->NS2BSIndx[0] = (0 << 1);
    p->NS2BSIndx[1] = (1 << 1);
    xx_rt_memset(p->NS2BSIndx + 2, (2 << 1), 9);
    xx_rt_memset(p->NS2BSIndx + 11, (3 << 1), 256 - 11);

    for (i = 0; i < 3; i++)
        p->NS2Indx[i] = (uint8_t)i;
    for (m = i, k = 1; i < 256; i++) {
        p->NS2Indx[i] = (uint8_t)m;
        if (--k == 0)
            k = (++m) - 2;
    }

    xx_rt_memset(p->HB2Flag, 0, 0x40);
    xx_rt_memset(p->HB2Flag + 0x40, 8, 0x100 - 0x40);
}

void Ppmd7_Free(CPpmd7 *p)
{
    if (p->Base) {
        xx_mem_free(p->Base);
        p->Base = NULL;
    }
    p->Size = 0;
}

bool Ppmd7_Alloc(CPpmd7 *p, uint32_t size)
{
    if (!p->Base || p->Size != size) {
        Ppmd7_Free(p);
        size_t size2 = UNIT_SIZE;
        p->AlignOffset = 4 - (size & 3);
        p->Base = (uint8_t *)xx_mem_alloc(p->AlignOffset + size + size2);
        if (!p->Base)
            return false;
        p->Size = size;
    }
    return true;
}

static void InsertNode(CPpmd7 *p, void *node, unsigned indx)
{
    *((CPpmd_Void_Ref *)node) = p->FreeList[indx];
    p->FreeList[indx] = REF(node);
}

static void *RemoveNode(CPpmd7 *p, unsigned indx)
{
    CPpmd_Void_Ref *node = (CPpmd_Void_Ref *)Ppmd7_GetPtr(p, p->FreeList[indx]);
    p->FreeList[indx] = *node;
    return node;
}

static void SplitBlock(CPpmd7 *p, void *ptr, unsigned oldIndx, unsigned newIndx)
{
    unsigned i, nu = (unsigned)I2U(oldIndx) - (unsigned)I2U(newIndx);
    ptr = (uint8_t *)ptr + U2B(I2U(newIndx));
    if (I2U(i = U2I(nu)) != nu) {
        unsigned k = I2U(--i);
        InsertNode(p, ((uint8_t *)ptr) + U2B(k), nu - k - 1);
    }
    InsertNode(p, ptr, i);
}

static void GlueFreeBlocks(CPpmd7 *p)
{
    uint32_t head = p->AlignOffset + p->Size;
    uint32_t n = head;
    unsigned i;

    p->GlueCount = 255;

    for (i = 0; i < PPMD_NUM_INDEXES; i++) {
        uint16_t nu = I2U(i);
        uint32_t next = p->FreeList[i];
        p->FreeList[i] = 0;
        while (next != 0) {
            CPpmd7_Node *node = NODE(next);
            node->Next = n;
            n = NODE(n)->Prev = next;
            next = *(const uint32_t *)node;
            node->Stamp = 0;
            node->NU = nu;
        }
    }
    NODE(head)->Stamp = 1;
    NODE(head)->Next = n;
    NODE(n)->Prev = head;
    if (p->LoUnit != p->HiUnit)
        ((CPpmd7_Node *)p->LoUnit)->Stamp = 1;

    while (n != head) {
        CPpmd7_Node *node = NODE(n);
        uint32_t nu = (uint32_t)node->NU;
        for (;;) {
            CPpmd7_Node *node2 = NODE(n) + nu;
            nu += node2->NU;
            if (node2->Stamp != 0 || nu >= 0x10000)
                break;
            NODE(node2->Prev)->Next = node2->Next;
            NODE(node2->Next)->Prev = node2->Prev;
            node->NU = (uint16_t)nu;
        }
        n = node->Next;
    }

    for (n = NODE(head)->Next; n != head;) {
        CPpmd7_Node *node = NODE(n);
        unsigned nu;
        uint32_t next = node->Next;
        for (nu = node->NU; nu > 128; nu -= 128, node += 128)
            InsertNode(p, node, PPMD_NUM_INDEXES - 1);
        if (I2U(i = U2I(nu)) != nu) {
            unsigned k = I2U(--i);
            InsertNode(p, node + k, nu - k - 1);
        }
        InsertNode(p, node, i);
        n = next;
    }
}

static void *AllocUnitsRare(CPpmd7 *p, unsigned indx)
{
    unsigned i;
    void *retVal;
    if (p->GlueCount == 0) {
        GlueFreeBlocks(p);
        if (p->FreeList[indx] != 0)
            return RemoveNode(p, indx);
    }
    i = indx;
    do {
        if (++i == PPMD_NUM_INDEXES) {
            uint32_t numBytes = U2B(I2U(indx));
            p->GlueCount--;
            return ((uint32_t)(p->UnitsStart - p->Text) > numBytes) ? (p->UnitsStart -= numBytes) : NULL;
        }
    } while (p->FreeList[i] == 0);
    retVal = RemoveNode(p, i);
    SplitBlock(p, retVal, i, indx);
    return retVal;
}

static void *AllocUnits(CPpmd7 *p, unsigned indx)
{
    uint32_t numBytes;
    if (p->FreeList[indx] != 0)
        return RemoveNode(p, indx);
    numBytes = U2B(I2U(indx));
    if (numBytes <= (uint32_t)(p->HiUnit - p->LoUnit)) {
        void *retVal = p->LoUnit;
        p->LoUnit += numBytes;
        return retVal;
    }
    return AllocUnitsRare(p, indx);
}

#define MyMem12Cpy(dest, src, num) \
    do { uint32_t *d = (uint32_t *)(dest); const uint32_t *s = (const uint32_t *)(src); uint32_t n = (num); \
        do { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; s += 3; d += 3; } while (--n); } while (0)

static void *ShrinkUnits(CPpmd7 *p, void *oldPtr, unsigned oldNU, unsigned newNU)
{
    unsigned i0 = U2I(oldNU);
    unsigned i1 = U2I(newNU);
    if (i0 == i1)
        return oldPtr;
    if (p->FreeList[i1] != 0) {
        void *ptr = RemoveNode(p, i1);
        MyMem12Cpy(ptr, oldPtr, newNU);
        InsertNode(p, oldPtr, i0);
        return ptr;
    }
    SplitBlock(p, oldPtr, i0, i1);
    return oldPtr;
}

#define SUCCESSOR(p) ((CPpmd_Void_Ref)((p)->SuccessorLow | ((uint32_t)(p)->SuccessorHigh << 16)))

static void SetSuccessor(CPpmd_State *p, CPpmd_Void_Ref v)
{
    p->SuccessorLow  = (uint16_t)((uint32_t)(v) & 0xFFFF);
    p->SuccessorHigh = (uint16_t)(((uint32_t)(v) >> 16) & 0xFFFF);
}

static void RestartModel(CPpmd7 *p)
{
    unsigned i, k, m;

    xx_rt_memset(p->FreeList, 0, sizeof(p->FreeList));
    p->Text = p->Base + p->AlignOffset;
    p->HiUnit = p->Text + p->Size;
    p->LoUnit = p->UnitsStart = p->HiUnit - p->Size / 8 / UNIT_SIZE * 7 * UNIT_SIZE;
    p->GlueCount = 0;

    p->OrderFall = p->MaxOrder;
    p->RunLength = p->InitRL = -(int32_t)((p->MaxOrder < 12) ? p->MaxOrder : 12) - 1;
    p->PrevSuccess = 0;

    p->MinContext = p->MaxContext = (CTX_PTR)(p->HiUnit -= UNIT_SIZE);
    p->MinContext->Suffix = 0;
    p->MinContext->NumStats = 256;
    p->MinContext->SummFreq = 256 + 1;
    p->FoundState = (CPpmd_State *)p->LoUnit;
    p->LoUnit += U2B(256 / 2);
    p->MinContext->Stats = REF(p->FoundState);
    for (i = 0; i < 256; i++) {
        CPpmd_State *s = &p->FoundState[i];
        s->Symbol = (uint8_t)i;
        s->Freq = 1;
        SetSuccessor(s, 0);
    }

    for (i = 0; i < 128; i++) {
        for (k = 0; k < 8; k++) {
            uint16_t *dest = p->BinSumm[i] + k;
            uint16_t val = (uint16_t)(PPMD_BIN_SCALE - kInitBinEsc[k] / (i + 2));
            for (m = 0; m < 64; m += 8)
                dest[m] = val;
        }
    }

    for (i = 0; i < 25; i++) {
        for (k = 0; k < 16; k++) {
            CPpmd_See *s = &p->See[i][k];
            s->Shift = (uint8_t)(PPMD_PERIOD_BITS - 4);
            s->Summ = (uint16_t)((5 * i + 10) << s->Shift);
            s->Count = 4;
        }
    }
}

void Ppmd7_Init(CPpmd7 *p, unsigned maxOrder)
{
    p->MaxOrder = maxOrder;
    RestartModel(p);
    p->DummySee.Shift = PPMD_PERIOD_BITS;
    p->DummySee.Summ = 0;
    p->DummySee.Count = 64;
}

static CTX_PTR CreateSuccessors(CPpmd7 *p, bool skip)
{
    CPpmd_State upState;
    CTX_PTR c = p->MinContext;
    CPpmd_Byte_Ref upBranch = (CPpmd_Byte_Ref)SUCCESSOR(p->FoundState);
    /* The model itself supports the original variant-H order range through
     * 64.  ZIP's public PPMd7 API intentionally remains capped at 16, while
     * RAR 3.x supplies model orders through 64 to this shared core. */
    CPpmd_State *ps[64];
    unsigned numPs = 0;

    if (!skip)
        ps[numPs++] = p->FoundState;

    while (c->Suffix) {
        CPpmd_Void_Ref successor;
        CPpmd_State *s;
        c = SUFFIX(c);
        if (c->NumStats != 1) {
            for (s = STATS(c); s->Symbol != p->FoundState->Symbol; s++);
        } else {
            s = ONE_STATE(c);
        }
        successor = SUCCESSOR(s);
        if (successor != upBranch) {
            c = CTX(successor);
            if (numPs == 0)
                return c;
            break;
        }
        ps[numPs++] = s;
    }

    upState.Symbol = *(const uint8_t *)Ppmd7_GetPtr(p, upBranch);
    SetSuccessor(&upState, upBranch + 1);

    if (c->NumStats == 1) {
        upState.Freq = ONE_STATE(c)->Freq;
    } else {
        uint32_t cf, s0;
        CPpmd_State *s;
        for (s = STATS(c); s->Symbol != upState.Symbol; s++);
        cf = s->Freq - 1;
        s0 = c->SummFreq - c->NumStats - cf;
        upState.Freq = (uint8_t)(1 + ((2 * cf <= s0) ? (5 * cf > s0) : ((2 * cf + 3 * s0 - 1) / (2 * s0))));
    }

    do {
        CTX_PTR c1;
        if (p->HiUnit != p->LoUnit)
            c1 = (CTX_PTR)(p->HiUnit -= UNIT_SIZE);
        else if (p->FreeList[0] != 0)
            c1 = (CTX_PTR)RemoveNode(p, 0);
        else {
            c1 = (CTX_PTR)AllocUnitsRare(p, 0);
            if (!c1)
                return NULL;
        }
        c1->NumStats = 1;
        *ONE_STATE(c1) = upState;
        c1->Suffix = REF(c);
        SetSuccessor(ps[--numPs], REF(c1));
        c = c1;
    } while (numPs != 0);

    return c;
}

static void SwapStates(CPpmd_State *t1, CPpmd_State *t2)
{
    CPpmd_State tmp = *t1;
    *t1 = *t2;
    *t2 = tmp;
}

static void UpdateModel(CPpmd7 *p)
{
    CPpmd_Void_Ref successor, fSuccessor = SUCCESSOR(p->FoundState);
    CTX_PTR c;
    unsigned s0, ns;

    if (p->FoundState->Freq < MAX_FREQ / 4 && p->MinContext->Suffix != 0) {
        c = SUFFIX(p->MinContext);
        if (c->NumStats == 1) {
            CPpmd_State *s = ONE_STATE(c);
            if (s->Freq < 32)
                s->Freq++;
        } else {
            CPpmd_State *s = STATS(c);
            if (s->Symbol != p->FoundState->Symbol) {
                do { s++; } while (s->Symbol != p->FoundState->Symbol);
                if (s[0].Freq >= s[-1].Freq) {
                    SwapStates(&s[0], &s[-1]);
                    s--;
                }
            }
            if (s->Freq < MAX_FREQ - 9) {
                s->Freq += 2;
                c->SummFreq += 2;
            }
        }
    }

    if (p->OrderFall == 0) {
        p->MinContext = p->MaxContext = CreateSuccessors(p, true);
        if (p->MinContext == 0) {
            RestartModel(p);
            return;
        }
        SetSuccessor(p->FoundState, REF(p->MinContext));
        return;
    }

    *p->Text++ = p->FoundState->Symbol;
    successor = REF(p->Text);
    if (p->Text >= p->UnitsStart) {
        RestartModel(p);
        return;
    }

    if (fSuccessor) {
        if (fSuccessor <= successor) {
            CTX_PTR cs = CreateSuccessors(p, false);
            if (cs == NULL) {
                RestartModel(p);
                return;
            }
            fSuccessor = REF(cs);
        }
        if (--p->OrderFall == 0) {
            successor = fSuccessor;
            p->Text -= (p->MaxContext != p->MinContext);
        }
    } else {
        SetSuccessor(p->FoundState, successor);
        fSuccessor = REF(p->MinContext);
    }

    s0 = p->MinContext->SummFreq - (ns = p->MinContext->NumStats) - (p->FoundState->Freq - 1);

    for (c = p->MaxContext; c != p->MinContext; c = SUFFIX(c)) {
        unsigned ns1;
        uint32_t cf, sf;
        if ((ns1 = c->NumStats) != 1) {
            if ((ns1 & 1) == 0) {
                unsigned oldNU = ns1 >> 1;
                unsigned i = U2I(oldNU);
                if (i != U2I((size_t)oldNU + 1)) {
                    void *ptr = AllocUnits(p, i + 1);
                    void *oldPtr;
                    if (!ptr) {
                        RestartModel(p);
                        return;
                    }
                    oldPtr = STATS(c);
                    MyMem12Cpy(ptr, oldPtr, oldNU);
                    InsertNode(p, oldPtr, i);
                    c->Stats = STATS_REF(ptr);
                }
            }
            c->SummFreq = (uint16_t)(c->SummFreq + (2 * ns1 < ns) + 2 * ((4 * ns1 <= ns) & (c->SummFreq <= 8 * ns1)));
        } else {
            CPpmd_State *s = (CPpmd_State *)AllocUnits(p, 0);
            if (!s) {
                RestartModel(p);
                return;
            }
            *s = *ONE_STATE(c);
            c->Stats = REF(s);
            if (s->Freq < MAX_FREQ / 4 - 1)
                s->Freq <<= 1;
            else
                s->Freq = MAX_FREQ - 4;
            c->SummFreq = (uint16_t)(s->Freq + p->InitEsc + (ns > 3));
        }
        cf = 2 * (uint32_t)p->FoundState->Freq * (c->SummFreq + 6);
        sf = (uint32_t)s0 + c->SummFreq;
        if (cf < 6 * sf) {
            cf = 1 + (cf > sf) + (cf >= 4 * sf);
            c->SummFreq += 3;
        } else {
            cf = 4 + (cf >= 9 * sf) + (cf >= 12 * sf) + (cf >= 15 * sf);
            c->SummFreq = (uint16_t)(c->SummFreq + cf);
        }
        {
            CPpmd_State *s = STATS(c) + ns1;
            SetSuccessor(s, successor);
            s->Symbol = p->FoundState->Symbol;
            s->Freq = (uint8_t)cf;
            c->NumStats = (uint16_t)(ns1 + 1);
        }
    }
    p->MaxContext = p->MinContext = CTX(fSuccessor);
}

static void Rescale(CPpmd7 *p)
{
    unsigned i, adder, sumFreq, escFreq;
    CPpmd_State *stats = STATS(p->MinContext);
    CPpmd_State *s = p->FoundState;
    {
        CPpmd_State tmp = *s;
        for (; s != stats; s--)
            s[0] = s[-1];
        *s = tmp;
    }
    escFreq = p->MinContext->SummFreq - s->Freq;
    s->Freq += 4;
    adder = (p->OrderFall != 0);
    s->Freq = (uint8_t)((s->Freq + adder) >> 1);
    sumFreq = s->Freq;

    i = p->MinContext->NumStats - 1;
    do {
        escFreq -= (++s)->Freq;
        s->Freq = (uint8_t)((s->Freq + adder) >> 1);
        sumFreq += s->Freq;
        if (s[0].Freq > s[-1].Freq) {
            CPpmd_State *s1 = s;
            CPpmd_State tmp = *s1;
            do {
                s1[0] = s1[-1];
            } while (--s1 != stats && tmp.Freq > s1[-1].Freq);
            *s1 = tmp;
        }
    } while (--i);

    if (s->Freq == 0) {
        unsigned numStats = p->MinContext->NumStats;
        unsigned n0, n1;
        do { i++; } while ((--s)->Freq == 0);
        escFreq += i;
        p->MinContext->NumStats = (uint16_t)(p->MinContext->NumStats - i);
        if (p->MinContext->NumStats == 1) {
            CPpmd_State tmp = *stats;
            do {
                tmp.Freq = (uint8_t)(tmp.Freq - (tmp.Freq >> 1));
                escFreq >>= 1;
            } while (escFreq > 1);
            InsertNode(p, stats, U2I(((numStats + 1) >> 1)));
            *(p->FoundState = ONE_STATE(p->MinContext)) = tmp;
            return;
        }
        n0 = (numStats + 1) >> 1;
        n1 = (p->MinContext->NumStats + 1) >> 1;
        if (n0 != n1)
            p->MinContext->Stats = STATS_REF(ShrinkUnits(p, stats, n0, n1));
    }
    p->MinContext->SummFreq = (uint16_t)(sumFreq + escFreq - (escFreq >> 1));
    p->FoundState = STATS(p->MinContext);
}

CPpmd_See *Ppmd7_MakeEscFreq(CPpmd7 *p, unsigned numMasked, uint32_t *escFreq)
{
    CPpmd_See *see;
    unsigned nonMasked = p->MinContext->NumStats - numMasked;
    if (p->MinContext->NumStats != 256) {
        see = p->See[(unsigned)p->NS2Indx[(size_t)nonMasked - 1]] +
            (nonMasked < (unsigned)SUFFIX(p->MinContext)->NumStats - p->MinContext->NumStats) +
            2 * (unsigned)(p->MinContext->SummFreq < 11 * p->MinContext->NumStats) +
            4 * (unsigned)(numMasked > nonMasked) +
            p->HiBitsFlag;
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

static void NextContext(CPpmd7 *p)
{
    CTX_PTR c = CTX(SUCCESSOR(p->FoundState));
    if (p->OrderFall == 0 && (uint8_t *)c > p->Text)
        p->MinContext = p->MaxContext = c;
    else
        UpdateModel(p);
}

void Ppmd7_Update1(CPpmd7 *p)
{
    CPpmd_State *s = p->FoundState;
    s->Freq += 4;
    p->MinContext->SummFreq += 4;
    if (s[0].Freq > s[-1].Freq) {
        SwapStates(&s[0], &s[-1]);
        p->FoundState = --s;
        if (s->Freq > MAX_FREQ)
            Rescale(p);
    }
    NextContext(p);
}

void Ppmd7_Update1_0(CPpmd7 *p)
{
    p->PrevSuccess = (2 * p->FoundState->Freq > p->MinContext->SummFreq);
    p->RunLength += p->PrevSuccess;
    p->MinContext->SummFreq += 4;
    if ((p->FoundState->Freq += 4) > MAX_FREQ)
        Rescale(p);
    NextContext(p);
}

void Ppmd7_UpdateBin(CPpmd7 *p)
{
    p->FoundState->Freq = (uint8_t)(p->FoundState->Freq + (p->FoundState->Freq < 128 ? 1 : 0));
    p->PrevSuccess = 1;
    p->RunLength++;
    NextContext(p);
}

void Ppmd7_Update2(CPpmd7 *p)
{
    p->MinContext->SummFreq += 4;
    if ((p->FoundState->Freq += 4) > MAX_FREQ)
        Rescale(p);
    p->RunLength = p->InitRL;
    UpdateModel(p);
}
