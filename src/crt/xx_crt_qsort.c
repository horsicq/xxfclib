/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_qsort.c
 * @brief qsort and qsort_s that permute exactly like the Universal CRT's.
 *
 * qsort is not stable, so which of two equal elements comes first depends
 * on the algorithm. Programs built with MSVC observe the order of UCRT's
 * quicksort: median-of-three pivot selection (first, middle, last), a
 * partition that skips elements equal to the pivot on both sides, a
 * selection sort for partitions of up to eight elements, and an explicit
 * stack that always defers the larger partition. This unit performs the
 * same comparisons in the same order (verified against UCRT by the
 * differential test in tests/crt) and differs only in how it exchanges
 * elements: word-sized and 16-byte moves instead of a byte loop.
 */

#include "xxfclib/crt/xx_crt.h"
#include "../memory/platforms/xx_memory_platform.h"

#include <stdint.h>

#define XX_CRT_QSORT_CUTOFF 8
#define XX_CRT_QSORT_STACK (8 * sizeof(void *) - 2)

static void xx_crt_qsort_swap(char *pLeft, char *pRight, size_t nWidth)
{
    if (pLeft == pRight) return;
    if (nWidth == 8) {
        uint64_t nLeft = xx_memory_load_word((const uint8_t *)pLeft);
        uint64_t nRight = xx_memory_load_word((const uint8_t *)pRight);
        xx_memory_store_word((uint8_t *)pLeft, nRight);
        xx_memory_store_word((uint8_t *)pRight, nLeft);
        return;
    }
    while (nWidth >= 8) {
        uint64_t nLeft = xx_memory_load_word((const uint8_t *)pLeft);
        uint64_t nRight = xx_memory_load_word((const uint8_t *)pRight);
        xx_memory_store_word((uint8_t *)pLeft, nRight);
        xx_memory_store_word((uint8_t *)pRight, nLeft);
        pLeft += 8;
        pRight += 8;
        nWidth -= 8;
    }
    while (nWidth--) {
        char nTemp = *pLeft;
        *pLeft++ = *pRight;
        *pRight++ = nTemp;
    }
}

/* The comparison callback in both shapes: qsort's (a, b) and qsort_s's
 * (context, a, b). */
typedef struct xx_crt_qsort_compare_s {
    int (*fnPlain)(const void *, const void *);
    int (*fnContext)(void *, const void *, const void *);
    void *pContext;
} xx_crt_qsort_compare;

static int xx_crt_qsort_call(const xx_crt_qsort_compare *pCompare, const void *pLeft, const void *pRight)
{
    if (pCompare->fnPlain) return pCompare->fnPlain(pLeft, pRight);
    return pCompare->fnContext(pCompare->pContext, pLeft, pRight);
}

/* Selection sort: repeatedly moves the largest remaining element (the first
 * of equals) to the end. */
static void xx_crt_qsort_short(char *pLow, char *pHigh, size_t nWidth, const xx_crt_qsort_compare *pCompare)
{
    while (pHigh > pLow) {
        char *pMax = pLow;
        char *p;
        for (p = pLow + nWidth; p <= pHigh; p += nWidth) {
            if (xx_crt_qsort_call(pCompare, p, pMax) > 0) pMax = p;
        }
        xx_crt_qsort_swap(pMax, pHigh, nWidth);
        pHigh -= nWidth;
    }
}

static void xx_crt_qsort_run(char *pBase, size_t nCount, size_t nWidth, const xx_crt_qsort_compare *pCompare)
{
    char *pLowStack[XX_CRT_QSORT_STACK];
    char *pHighStack[XX_CRT_QSORT_STACK];
    int nStack = 0;
    char *pLow;
    char *pHigh;

    if (nCount < 2 || nWidth == 0) return;
    pLow = pBase;
    pHigh = pBase + nWidth * (nCount - 1);

    for (;;) {
        size_t nSize = (size_t)(pHigh - pLow) / nWidth + 1;
        if (nSize <= XX_CRT_QSORT_CUTOFF) {
            xx_crt_qsort_short(pLow, pHigh, nWidth, pCompare);
        } else {
            char *pMiddle = pLow + (nSize / 2) * nWidth;
            char *pLeft = pLow;
            char *pRight = pHigh;

            if (xx_crt_qsort_call(pCompare, pLow, pMiddle) > 0) xx_crt_qsort_swap(pLow, pMiddle, nWidth);
            if (xx_crt_qsort_call(pCompare, pLow, pHigh) > 0) xx_crt_qsort_swap(pLow, pHigh, nWidth);
            if (xx_crt_qsort_call(pCompare, pMiddle, pHigh) > 0) xx_crt_qsort_swap(pMiddle, pHigh, nWidth);

            for (;;) {
                if (pMiddle > pLeft) {
                    do {
                        pLeft += nWidth;
                    } while (pLeft < pMiddle && xx_crt_qsort_call(pCompare, pLeft, pMiddle) <= 0);
                }
                if (pMiddle <= pLeft) {
                    do {
                        pLeft += nWidth;
                    } while (pLeft <= pHigh && xx_crt_qsort_call(pCompare, pLeft, pMiddle) <= 0);
                }
                do {
                    pRight -= nWidth;
                } while (pRight > pMiddle && xx_crt_qsort_call(pCompare, pRight, pMiddle) > 0);
                if (pRight < pLeft) break;
                xx_crt_qsort_swap(pLeft, pRight, nWidth);
                /* the pivot moved with the exchange */
                if (pMiddle == pRight) pMiddle = pLeft;
            }

            /* Skip the elements equal to the pivot on the left side. */
            pRight += nWidth;
            if (pMiddle < pRight) {
                do {
                    pRight -= nWidth;
                } while (pRight > pMiddle && xx_crt_qsort_call(pCompare, pRight, pMiddle) == 0);
            }
            if (pMiddle >= pRight) {
                do {
                    pRight -= nWidth;
                } while (pRight > pLow && xx_crt_qsort_call(pCompare, pRight, pMiddle) == 0);
            }

            /* [pLow, pRight] and [pLeft, pHigh] remain; defer the larger. */
            if (pRight - pLow >= pHigh - pLeft) {
                if (pLow < pRight) {
                    pLowStack[nStack] = pLow;
                    pHighStack[nStack] = pRight;
                    ++nStack;
                }
                if (pLeft < pHigh) {
                    pLow = pLeft;
                    continue;
                }
            } else {
                if (pLeft < pHigh) {
                    pLowStack[nStack] = pLeft;
                    pHighStack[nStack] = pHigh;
                    ++nStack;
                }
                if (pLow < pRight) {
                    pHigh = pRight;
                    continue;
                }
            }
        }
        if (nStack == 0) return;
        --nStack;
        pLow = pLowStack[nStack];
        pHigh = pHighStack[nStack];
    }
}

void xx_crt_qsort(void *pBase, size_t nCount, size_t nSize, int (*fnCompare)(const void *, const void *))
{
    xx_crt_qsort_compare compare;
    if (!pBase || !fnCompare) return;
    compare.fnPlain = fnCompare;
    compare.fnContext = NULL;
    compare.pContext = NULL;
    xx_crt_qsort_run((char *)pBase, nCount, nSize, &compare);
}

void xx_crt_qsort_s(void *pBase, size_t nCount, size_t nSize, int (*fnCompare)(void *, const void *, const void *), void *pContext)
{
    xx_crt_qsort_compare compare;
    if (!pBase || !fnCompare) return;
    compare.fnPlain = NULL;
    compare.fnContext = fnCompare;
    compare.pContext = pContext;
    xx_crt_qsort_run((char *)pBase, nCount, nSize, &compare);
}
