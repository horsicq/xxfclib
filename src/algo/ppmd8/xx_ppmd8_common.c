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

/* PPMd8 model core and sub-allocator.
 * Algorithm by Dmitry Shkarin (public domain, 2002).
 * Adaptation by Igor Pavlov (public domain, 2018).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_ppmd8_internal.h"
#include <string.h>

const uint8_t PPMD8_kExpEscape[16] = { 25, 14, 9, 7, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 2 };
static const uint16_t kInitBinEsc[] = { 0x3CDD, 0x1F3F, 0x59BF, 0x48F3, 0x64A1, 0x5ABC, 0x6632, 0x6051 };

#define MAX_FREQ  PPMD8_MAX_FREQ
#define UNIT_SIZE PPMD8_UNIT_SIZE

#define U2B(nu) ((uint32_t)(nu) * UNIT_SIZE)
#define U2I(nu) (p->Units2Indx[(size_t)(nu) - 1])
#define I2U(indx) (p->Indx2Units[indx])

#define REF(ptr) ((uint32_t)((uint8_t *)(ptr) - (p)->Base))
#define STATS_REF(ptr) ((CPpmd_State_Ref)REF(ptr))

#define CTX(ref) ((CPpmd8_Context *)Ppmd8_GetContext(p, ref))
#define STATS(ctx) Ppmd8_GetStats(p, ctx)
#define ONE_STATE(ctx) Ppmd8Context_OneState(ctx)
#define SUFFIX(ctx) CTX((ctx)->Suffix)

typedef CPpmd8_Context * CTX_PTR;

typedef uint32_t CPpmd8_Node_Ref;

typedef struct CPpmd8_Node_ {
    uint32_t        Stamp;
    CPpmd8_Node_Ref Next;
    uint32_t        NU;
} CPpmd8_Node;

#define NODE(offs) ((CPpmd8_Node *)(p->Base + (offs)))
#define EMPTY_NODE 0xFFFFFFFFu

void Ppmd8_Construct(CPpmd8 *p)
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

    for (i = 0; i < 5; i++)
        p->NS2Indx[i] = (uint8_t)i;
    for (m = i, k = 1; i < 260; i++) {
        p->NS2Indx[i] = (uint8_t)m;
        if (--k == 0)
            k = (++m) - 4;
    }
}

void Ppmd8_Free(CPpmd8 *p)
{
    if (p->Base) {
        xx_mem_free(p->Base);
        p->Base = NULL;
    }
    p->Size = 0;
}

bool Ppmd8_Alloc(CPpmd8 *p, uint32_t size)
{
    if (!p->Base || p->Size != size) {
        Ppmd8_Free(p);
        p->AlignOffset = 4 - (size & 3);
        p->Base = (uint8_t *)xx_mem_alloc(p->AlignOffset + size);
        if (!p->Base)
            return false;
        p->Size = size;
    }
    return true;
}

static void InsertNode(CPpmd8 *p, void *node, unsigned indx)
{
    ((CPpmd8_Node *)node)->Stamp = EMPTY_NODE;
    ((CPpmd8_Node *)node)->Next = (CPpmd8_Node_Ref)p->FreeList[indx];
    ((CPpmd8_Node *)node)->NU = I2U(indx);
    p->FreeList[indx] = REF(node);
    p->Stamps[indx]++;
}

static void *RemoveNode(CPpmd8 *p, unsigned indx)
{
    CPpmd8_Node *node = NODE((CPpmd8_Node_Ref)p->FreeList[indx]);
    p->FreeList[indx] = node->Next;
    p->Stamps[indx]--;
    return node;
}

static void SplitBlock(CPpmd8 *p, void *ptr, unsigned oldIndx, unsigned newIndx)
{
    unsigned i, nu = I2U(oldIndx) - I2U(newIndx);
    ptr = (uint8_t *)ptr + U2B(I2U(newIndx));
    if (I2U(i = U2I(nu)) != nu) {
        unsigned k = I2U(--i);
        InsertNode(p, ((uint8_t *)ptr) + U2B(k), nu - k - 1);
    }
    InsertNode(p, ptr, i);
}

static void GlueFreeBlocks(CPpmd8 *p)
{
    CPpmd8_Node_Ref head = 0;
    CPpmd8_Node_Ref *prev = &head;
    unsigned i;

    p->GlueCount = 1 << 13;
    xx_rt_memset(p->Stamps, 0, sizeof(p->Stamps));

    if (p->LoUnit != p->HiUnit)
        ((CPpmd8_Node *)p->LoUnit)->Stamp = 0;

    for (i = 0; i < PPMD_NUM_INDEXES; i++) {
        CPpmd8_Node_Ref next = (CPpmd8_Node_Ref)p->FreeList[i];
        p->FreeList[i] = 0;
        while (next != 0) {
            CPpmd8_Node *node = NODE(next);
            if (node->NU != 0) {
                CPpmd8_Node *node2;
                *prev = next;
                prev = &(node->Next);
                while ((node2 = node + node->NU)->Stamp == EMPTY_NODE) {
                    node->NU += node2->NU;
                    node2->NU = 0;
                }
            }
            next = node->Next;
        }
    }
    *prev = 0;

    while (head != 0) {
        CPpmd8_Node *node = NODE(head);
        unsigned nu;
        head = node->Next;
        nu = node->NU;
        if (nu == 0)
            continue;
        for (; nu > 128; nu -= 128, node += 128)
            InsertNode(p, node, PPMD_NUM_INDEXES - 1);
        if (I2U(i = U2I(nu)) != nu) {
            unsigned k = I2U(--i);
            InsertNode(p, node + k, nu - k - 1);
        }
        InsertNode(p, node, i);
    }
}

static void *AllocUnitsRare(CPpmd8 *p, unsigned indx)
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

static void *AllocUnits(CPpmd8 *p, unsigned indx)
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
    { uint32_t *d = (uint32_t *)(dest); const uint32_t *z = (const uint32_t *)(src); uint32_t n = (num); \
      do { d[0] = z[0]; d[1] = z[1]; d[2] = z[2]; z += 3; d += 3; } while (--n); }

static void *ShrinkUnits(CPpmd8 *p, void *oldPtr, unsigned oldNU, unsigned newNU)
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

static void FreeUnits(CPpmd8 *p, void *ptr, unsigned nu)
{
    InsertNode(p, ptr, U2I(nu));
}

static void SpecialFreeUnit(CPpmd8 *p, void *ptr)
{
    if ((uint8_t *)ptr != p->UnitsStart)
        InsertNode(p, ptr, 0);
    else
        p->UnitsStart += UNIT_SIZE;
}

static void *MoveUnitsUp(CPpmd8 *p, void *oldPtr, unsigned nu)
{
    unsigned indx = U2I(nu);
    void *ptr;
    if ((uint8_t *)oldPtr > p->UnitsStart + 16 * 1024 || REF(oldPtr) > p->FreeList[indx])
        return oldPtr;
    ptr = RemoveNode(p, indx);
    MyMem12Cpy(ptr, oldPtr, nu);
    if ((uint8_t *)oldPtr != p->UnitsStart)
        InsertNode(p, oldPtr, indx);
    else
        p->UnitsStart += U2B(I2U(indx));
    return ptr;
}

static void ExpandTextArea(CPpmd8 *p)
{
    uint32_t count[PPMD_NUM_INDEXES];
    unsigned i;
    xx_rt_memset(count, 0, sizeof(count));
    if (p->LoUnit != p->HiUnit)
        ((CPpmd8_Node *)p->LoUnit)->Stamp = 0;

    {
        CPpmd8_Node *node = (CPpmd8_Node *)p->UnitsStart;
        for (; node->Stamp == EMPTY_NODE; node += node->NU) {
            node->Stamp = 0;
            count[U2I(node->NU)]++;
        }
        p->UnitsStart = (uint8_t *)node;
    }

    for (i = 0; i < PPMD_NUM_INDEXES; i++) {
        CPpmd8_Node_Ref *next = (CPpmd8_Node_Ref *)&p->FreeList[i];
        while (count[i] != 0) {
            CPpmd8_Node *node = NODE(*next);
            while (node->Stamp == 0) {
                *next = node->Next;
                node = NODE(*next);
                p->Stamps[i]--;
                if (--count[i] == 0)
                    break;
            }
            next = &node->Next;
        }
    }
}

#define SUCCESSOR(p) ((CPpmd_Void_Ref)((p)->SuccessorLow | ((uint32_t)(p)->SuccessorHigh << 16)))

static void SetSuccessor(CPpmd_State *p, CPpmd_Void_Ref v)
{
    p->SuccessorLow = (uint16_t)((uint32_t)v & 0xFFFF);
    p->SuccessorHigh = (uint16_t)(((uint32_t)v >> 16) & 0xFFFF);
}

#define RESET_TEXT(offs) { p->Text = p->Base + p->AlignOffset + (offs); }

static void RestartModel(CPpmd8 *p)
{
    unsigned i, k, m, r;

    xx_rt_memset(p->FreeList, 0, sizeof(p->FreeList));
    xx_rt_memset(p->Stamps, 0, sizeof(p->Stamps));
    RESET_TEXT(0);
    p->HiUnit = p->Text + p->Size;
    p->LoUnit = p->UnitsStart = p->HiUnit - p->Size / 8 / UNIT_SIZE * 7 * UNIT_SIZE;
    p->GlueCount = 0;

    p->OrderFall = p->MaxOrder;
    p->RunLength = p->InitRL = -(int32_t)((p->MaxOrder < 12) ? p->MaxOrder : 12) - 1;
    p->PrevSuccess = 0;

    p->MinContext = p->MaxContext = (CTX_PTR)(p->HiUnit -= UNIT_SIZE);
    p->MinContext->Suffix = 0;
    p->MinContext->NumStats = 255;
    p->MinContext->Flags = 0;
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

    for (i = m = 0; m < 25; m++) {
        while (p->NS2Indx[i] == m)
            i++;
        for (k = 0; k < 8; k++) {
            uint16_t val = (uint16_t)(PPMD_BIN_SCALE - kInitBinEsc[k] / (i + 1));
            uint16_t *dest = p->BinSumm[m] + k;
            for (r = 0; r < 64; r += 8)
                dest[r] = val;
        }
    }

    for (i = m = 0; m < 24; m++) {
        while (p->NS2Indx[(size_t)i + 3] == m + 3)
            i++;
        for (k = 0; k < 32; k++) {
            CPpmd_See *s = &p->See[m][k];
            s->Summ = (uint16_t)((2 * i + 5) << (s->Shift = PPMD_PERIOD_BITS - 4));
            s->Count = 7;
        }
    }
}

void Ppmd8_Init(CPpmd8 *p, unsigned maxOrder, unsigned restoreMethod)
{
    p->MaxOrder = maxOrder;
    p->RestoreMethod = restoreMethod;
    RestartModel(p);
    p->DummySee.Shift = PPMD_PERIOD_BITS;
    p->DummySee.Summ = 0;
    p->DummySee.Count = 64;
}

static void Refresh(CPpmd8 *p, CTX_PTR ctx, unsigned oldNU, unsigned scale)
{
    unsigned i = ctx->NumStats, escFreq, sumFreq, flags;
    CPpmd_State *s = (CPpmd_State *)ShrinkUnits(p, STATS(ctx), oldNU, (i + 2) >> 1);
    ctx->Stats = REF(s);
    flags = (ctx->Flags & (0x10 + 0x04 * scale)) + 0x08 * (s->Symbol >= 0x40);
    escFreq = ctx->SummFreq - s->Freq;
    sumFreq = (s->Freq = (uint8_t)((s->Freq + scale) >> scale));
    do {
        escFreq -= (++s)->Freq;
        sumFreq += (s->Freq = (uint8_t)((s->Freq + scale) >> scale));
        flags |= 0x08 * (s->Symbol >= 0x40);
    } while (--i);
    ctx->SummFreq = (uint16_t)(sumFreq + ((escFreq + scale) >> scale));
    ctx->Flags = (uint8_t)flags;
}

static void SwapStates(CPpmd_State *t1, CPpmd_State *t2)
{
    CPpmd_State tmp = *t1;
    *t1 = *t2;
    *t2 = tmp;
}

static CPpmd_Void_Ref CutOff(CPpmd8 *p, CTX_PTR ctx, unsigned order)
{
    int i;
    unsigned tmp;
    CPpmd_State *s;

    if (!ctx->NumStats) {
        s = ONE_STATE(ctx);
        if ((uint8_t *)Ppmd8_GetPtr(p, SUCCESSOR(s)) >= p->UnitsStart) {
            if (order < p->MaxOrder)
                SetSuccessor(s, CutOff(p, CTX(SUCCESSOR(s)), order + 1));
            else
                SetSuccessor(s, 0);
            if (SUCCESSOR(s) || order <= 9)
                return REF(ctx);
        }
        SpecialFreeUnit(p, ctx);
        return 0;
    }

    ctx->Stats = STATS_REF(MoveUnitsUp(p, STATS(ctx), tmp = ((unsigned)ctx->NumStats + 2) >> 1));

    for (s = STATS(ctx) + (i = ctx->NumStats); s >= STATS(ctx); s--) {
        if ((uint8_t *)Ppmd8_GetPtr(p, SUCCESSOR(s)) < p->UnitsStart) {
            CPpmd_State *s2 = STATS(ctx) + (i--);
            SetSuccessor(s, 0);
            SwapStates(s, s2);
        } else if (order < p->MaxOrder) {
            SetSuccessor(s, CutOff(p, CTX(SUCCESSOR(s)), order + 1));
        } else {
            SetSuccessor(s, 0);
        }
    }

    if (i != ctx->NumStats && order) {
        ctx->NumStats = (uint8_t)i;
        s = STATS(ctx);
        if (i < 0) {
            FreeUnits(p, s, tmp);
            SpecialFreeUnit(p, ctx);
            return 0;
        }
        if (i == 0) {
            ctx->Flags = (uint8_t)((ctx->Flags & 0x10) + 0x08 * (s->Symbol >= 0x40));
            *ONE_STATE(ctx) = *s;
            FreeUnits(p, s, tmp);
            ONE_STATE(ctx)->Freq = (uint8_t)(((unsigned)ONE_STATE(ctx)->Freq + 11) >> 3);
        } else {
            Refresh(p, ctx, tmp, ctx->SummFreq > 16 * (unsigned)i);
        }
    }
    return REF(ctx);
}

static uint32_t GetUsedMemory(const CPpmd8 *p)
{
    uint32_t v = 0;
    unsigned i;
    for (i = 0; i < PPMD_NUM_INDEXES; i++)
        v += p->Stamps[i] * I2U(i);
    return p->Size - (uint32_t)(p->HiUnit - p->LoUnit) - (uint32_t)(p->UnitsStart - p->Text) - U2B(v);
}

static void RestoreModel(CPpmd8 *p, CTX_PTR c1)
{
    CTX_PTR c;
    CPpmd_State *s;
    RESET_TEXT(0);
    for (c = p->MaxContext; c != c1; c = SUFFIX(c)) {
        if (--(c->NumStats) == 0) {
            s = STATS(c);
            c->Flags = (uint8_t)((c->Flags & 0x10) + 0x08 * (s->Symbol >= 0x40));
            *ONE_STATE(c) = *s;
            SpecialFreeUnit(p, s);
            ONE_STATE(c)->Freq = (uint8_t)(((unsigned)ONE_STATE(c)->Freq + 11) >> 3);
        } else {
            Refresh(p, c, (c->NumStats + 3) >> 1, 0);
        }
    }

    for (; c != p->MinContext; c = SUFFIX(c)) {
        if (!c->NumStats)
            ONE_STATE(c)->Freq = (uint8_t)(ONE_STATE(c)->Freq - (ONE_STATE(c)->Freq >> 1));
        else if ((c->SummFreq += 4) > 128 + 4 * c->NumStats)
            Refresh(p, c, (c->NumStats + 2) >> 1, 1);
    }

    if (p->RestoreMethod == XX_PPMD8_RESTORE_METHOD_RESTART || GetUsedMemory(p) < (p->Size >> 1)) {
        RestartModel(p);
    } else {
        while (p->MaxContext->Suffix)
            p->MaxContext = SUFFIX(p->MaxContext);
        do {
            CutOff(p, p->MaxContext, 0);
            ExpandTextArea(p);
        } while (GetUsedMemory(p) > 3 * (p->Size >> 2));
        p->GlueCount = 0;
        p->OrderFall = p->MaxOrder;
    }
}

static CTX_PTR CreateSuccessors(CPpmd8 *p, bool skip, CPpmd_State *s1, CTX_PTR c)
{
    CPpmd_State upState;
    uint8_t flags;
    CPpmd_Byte_Ref upBranch = (CPpmd_Byte_Ref)SUCCESSOR(p->FoundState);
    CPpmd_State *ps[PPMD8_MAX_ORDER + 1];
    unsigned numPs = 0;

    if (!skip)
        ps[numPs++] = p->FoundState;

    while (c->Suffix) {
        CPpmd_Void_Ref successor;
        CPpmd_State *s;
        c = SUFFIX(c);
        if (s1) {
            s = s1;
            s1 = NULL;
        } else if (c->NumStats != 0) {
            for (s = STATS(c); s->Symbol != p->FoundState->Symbol; s++);
            if (s->Freq < MAX_FREQ - 9) {
                s->Freq++;
                c->SummFreq++;
            }
        } else {
            s = ONE_STATE(c);
            s->Freq = (uint8_t)(s->Freq + (!SUFFIX(c)->NumStats & (s->Freq < 24)));
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

    upState.Symbol = *(const uint8_t *)Ppmd8_GetPtr(p, upBranch);
    SetSuccessor(&upState, upBranch + 1);
    flags = (uint8_t)(0x10 * (p->FoundState->Symbol >= 0x40) + 0x08 * (upState.Symbol >= 0x40));

    if (c->NumStats == 0) {
        upState.Freq = ONE_STATE(c)->Freq;
    } else {
        uint32_t cf, s0;
        CPpmd_State *s;
        for (s = STATS(c); s->Symbol != upState.Symbol; s++);
        cf = s->Freq - 1;
        s0 = c->SummFreq - c->NumStats - cf;
        upState.Freq = (uint8_t)(1 + ((2 * cf <= s0) ? (5 * cf > s0) : ((cf + 2 * s0 - 3) / s0)));
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
        c1->NumStats = 0;
        c1->Flags = flags;
        *ONE_STATE(c1) = upState;
        c1->Suffix = REF(c);
        SetSuccessor(ps[--numPs], REF(c1));
        c = c1;
    } while (numPs != 0);

    return c;
}

static CTX_PTR ReduceOrder(CPpmd8 *p, CPpmd_State *s1, CTX_PTR c)
{
    CPpmd_State *s = NULL;
    CTX_PTR c1 = c;
    CPpmd_Void_Ref upBranch = REF(p->Text);

    SetSuccessor(p->FoundState, upBranch);
    p->OrderFall++;

    for (;;) {
        if (s1) {
            c = SUFFIX(c);
            s = s1;
            s1 = NULL;
        } else {
            if (!c->Suffix)
                return c;
            c = SUFFIX(c);
            if (c->NumStats) {
                if ((s = STATS(c))->Symbol != p->FoundState->Symbol)
                    do { s++; } while (s->Symbol != p->FoundState->Symbol);
                if (s->Freq < MAX_FREQ - 9) {
                    s->Freq += 2;
                    c->SummFreq += 2;
                }
            } else {
                s = ONE_STATE(c);
                s->Freq = (uint8_t)(s->Freq + (s->Freq < 32));
            }
        }
        if (SUCCESSOR(s))
            break;
        SetSuccessor(s, upBranch);
        p->OrderFall++;
    }

    if (SUCCESSOR(s) <= upBranch) {
        CTX_PTR successor;
        CPpmd_State *s2 = p->FoundState;
        p->FoundState = s;

        successor = CreateSuccessors(p, false, NULL, c);
        if (successor == NULL)
            SetSuccessor(s, 0);
        else
            SetSuccessor(s, REF(successor));
        p->FoundState = s2;
    }

    if (p->OrderFall == 1 && c1 == p->MaxContext) {
        SetSuccessor(p->FoundState, SUCCESSOR(s));
        p->Text--;
    }
    if (SUCCESSOR(s) == 0)
        return NULL;
    return CTX(SUCCESSOR(s));
}

static void UpdateModel(CPpmd8 *p)
{
    CPpmd_Void_Ref successor, fSuccessor = SUCCESSOR(p->FoundState);
    CTX_PTR c;
    unsigned s0, ns, fFreq = p->FoundState->Freq;
    uint8_t flag, fSymbol = p->FoundState->Symbol;
    CPpmd_State *s = NULL;

    if (p->FoundState->Freq < MAX_FREQ / 4 && p->MinContext->Suffix != 0) {
        c = SUFFIX(p->MinContext);
        if (c->NumStats == 0) {
            s = ONE_STATE(c);
            if (s->Freq < 32)
                s->Freq++;
        } else {
            s = STATS(c);
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

    c = p->MaxContext;
    if (p->OrderFall == 0 && fSuccessor) {
        CTX_PTR cs = CreateSuccessors(p, true, s, p->MinContext);
        if (cs == 0) {
            SetSuccessor(p->FoundState, 0);
            RestoreModel(p, c);
        } else {
            SetSuccessor(p->FoundState, REF(cs));
            p->MaxContext = cs;
        }
        return;
    }

    *p->Text++ = p->FoundState->Symbol;
    successor = REF(p->Text);
    if (p->Text >= p->UnitsStart) {
        RestoreModel(p, c);
        return;
    }

    if (!fSuccessor) {
        CTX_PTR cs = ReduceOrder(p, s, p->MinContext);
        if (cs == NULL) {
            RestoreModel(p, c);
            return;
        }
        fSuccessor = REF(cs);
    } else if ((uint8_t *)Ppmd8_GetPtr(p, fSuccessor) < p->UnitsStart) {
        CTX_PTR cs = CreateSuccessors(p, false, s, p->MinContext);
        if (cs == NULL) {
            RestoreModel(p, c);
            return;
        }
        fSuccessor = REF(cs);
    }

    if (--p->OrderFall == 0) {
        successor = fSuccessor;
        p->Text -= (p->MaxContext != p->MinContext);
    }

    s0 = p->MinContext->SummFreq - (ns = p->MinContext->NumStats) - fFreq;
    flag = (uint8_t)(0x08 * (fSymbol >= 0x40));

    for (; c != p->MinContext; c = SUFFIX(c)) {
        unsigned ns1;
        uint32_t cf, sf;
        if ((ns1 = c->NumStats) != 0) {
            if ((ns1 & 1) != 0) {
                unsigned oldNU = (ns1 + 1) >> 1;
                unsigned i = U2I(oldNU);
                if (i != U2I((size_t)oldNU + 1)) {
                    void *ptr = AllocUnits(p, i + 1);
                    void *oldPtr;
                    if (!ptr) {
                        RestoreModel(p, c);
                        return;
                    }
                    oldPtr = STATS(c);
                    MyMem12Cpy(ptr, oldPtr, oldNU);
                    InsertNode(p, oldPtr, i);
                    c->Stats = STATS_REF(ptr);
                }
            }
            c->SummFreq = (uint16_t)(c->SummFreq + (3 * ns1 + 1 < ns));
        } else {
            CPpmd_State *s2 = (CPpmd_State *)AllocUnits(p, 0);
            if (!s2) {
                RestoreModel(p, c);
                return;
            }
            *s2 = *ONE_STATE(c);
            c->Stats = REF(s2);
            if (s2->Freq < MAX_FREQ / 4 - 1)
                s2->Freq <<= 1;
            else
                s2->Freq = MAX_FREQ - 4;
            c->SummFreq = (uint16_t)(s2->Freq + p->InitEsc + (ns > 2));
        }
        cf = 2 * fFreq * (c->SummFreq + 6);
        sf = (uint32_t)s0 + c->SummFreq;
        if (cf < 6 * sf) {
            cf = 1 + (cf > sf) + (cf >= 4 * sf);
            c->SummFreq += 4;
        } else {
            cf = 4 + (cf > 9 * sf) + (cf > 12 * sf) + (cf > 15 * sf);
            c->SummFreq = (uint16_t)(c->SummFreq + cf);
        }
        {
            CPpmd_State *s2 = STATS(c) + ns1 + 1;
            SetSuccessor(s2, successor);
            s2->Symbol = fSymbol;
            s2->Freq = (uint8_t)cf;
            c->Flags |= flag;
            c->NumStats = (uint8_t)(ns1 + 1);
        }
    }
    p->MaxContext = p->MinContext = CTX(fSuccessor);
}

static void Rescale(CPpmd8 *p)
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

    i = p->MinContext->NumStats;
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
        p->MinContext->NumStats = (uint8_t)(p->MinContext->NumStats - i);
        if (p->MinContext->NumStats == 0) {
            CPpmd_State tmp = *stats;
            tmp.Freq = (uint8_t)((2 * tmp.Freq + escFreq - 1) / escFreq);
            if (tmp.Freq > MAX_FREQ / 3)
                tmp.Freq = MAX_FREQ / 3;
            InsertNode(p, stats, U2I((numStats + 2) >> 1));
            p->MinContext->Flags = (uint8_t)((p->MinContext->Flags & 0x10) + 0x08 * (tmp.Symbol >= 0x40));
            *(p->FoundState = ONE_STATE(p->MinContext)) = tmp;
            return;
        }
        n0 = (numStats + 2) >> 1;
        n1 = (p->MinContext->NumStats + 2) >> 1;
        if (n0 != n1)
            p->MinContext->Stats = STATS_REF(ShrinkUnits(p, stats, n0, n1));
        p->MinContext->Flags &= ~0x08;
        p->MinContext->Flags |= 0x08 * ((s = STATS(p->MinContext))->Symbol >= 0x40);
        i = p->MinContext->NumStats;
        do { p->MinContext->Flags |= 0x08 * ((++s)->Symbol >= 0x40); } while (--i);
    }
    p->MinContext->SummFreq = (uint16_t)(sumFreq + escFreq - (escFreq >> 1));
    p->MinContext->Flags |= 0x4;
    p->FoundState = STATS(p->MinContext);
}

CPpmd_See *Ppmd8_MakeEscFreq(CPpmd8 *p, unsigned numMasked1, uint32_t *escFreq)
{
    CPpmd_See *see;
    if (p->MinContext->NumStats != 0xFF) {
        see = p->See[(size_t)(unsigned)p->NS2Indx[(size_t)(unsigned)p->MinContext->NumStats + 2] - 3] +
            (p->MinContext->SummFreq > 11 * ((unsigned)p->MinContext->NumStats + 1)) +
            2 * (unsigned)(2 * (unsigned)p->MinContext->NumStats <
            ((unsigned)SUFFIX(p->MinContext)->NumStats + numMasked1)) +
            p->MinContext->Flags;
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

static void NextContext(CPpmd8 *p)
{
    CTX_PTR c = CTX(SUCCESSOR(p->FoundState));
    if (p->OrderFall == 0 && (uint8_t *)c >= p->UnitsStart)
        p->MinContext = p->MaxContext = c;
    else {
        UpdateModel(p);
        p->MinContext = p->MaxContext;
    }
}

void Ppmd8_Update1(CPpmd8 *p)
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

void Ppmd8_Update1_0(CPpmd8 *p)
{
    p->PrevSuccess = (2 * p->FoundState->Freq >= p->MinContext->SummFreq);
    p->RunLength += p->PrevSuccess;
    p->MinContext->SummFreq += 4;
    if ((p->FoundState->Freq += 4) > MAX_FREQ)
        Rescale(p);
    NextContext(p);
}

void Ppmd8_UpdateBin(CPpmd8 *p)
{
    p->FoundState->Freq = (uint8_t)(p->FoundState->Freq + (p->FoundState->Freq < 196));
    p->PrevSuccess = 1;
    p->RunLength++;
    NextContext(p);
}

void Ppmd8_Update2(CPpmd8 *p)
{
    p->MinContext->SummFreq += 4;
    if ((p->FoundState->Freq += 4) > MAX_FREQ)
        Rescale(p);
    p->RunLength = p->InitRL;
    UpdateModel(p);
    p->MinContext = p->MaxContext;
}
