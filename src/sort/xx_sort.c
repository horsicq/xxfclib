/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_sort.c
 * @brief Allocation-free sorting implementations shared by all platforms.
 */

#include "xx_sort_internal.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "../memory/platforms/xx_memory_platform.h"

static void xx_sort_swap_bytes(char *pLeft, char *pRight, size_t nSize)
{
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        char nTemp = pLeft[i];

        pLeft[i] = pRight[i];
        pRight[i] = nTemp;
    }
}

void xx_sort_swap_scalar(char *pLeft, char *pRight, size_t nSize)
{
    xx_sort_swap_bytes(pLeft, pRight, nSize);
}

#define XX_SORT_IMPL(name) name##_scalar
#include "xx_sort_algorithms.inc"
#undef XX_SORT_IMPL

typedef struct xx_sort_backend_s {
    xx_sort_swap_fn fnSwap;
    xx_sort_copy_fn fnCopy;
    void (*fnContext)(void *, size_t, size_t, xx_sort_compare_context_fn, void *);
    void (*fnQuick)(void *, size_t, size_t, xx_sort_compare_fn);
    void (*fnStable)(void *, size_t, size_t, xx_sort_compare_fn);
} xx_sort_backend;

static const xx_sort_backend *xx_sort_select_backend(size_t nSize)
{
    static const xx_sort_backend scalar = {xx_sort_swap_scalar, xx_memory_copy_scalar, xx_sort_context_scalar, xx_sort_quick_scalar, xx_sort_stable_scalar};
#ifdef XX_SORT_X86
    static const xx_sort_backend sse2 = {xx_sort_swap_sse2, xx_memory_copy_sse2, xx_sort_context_sse2, xx_sort_quick_sse2, xx_sort_stable_sse2};
    static const xx_sort_backend avx2 = {xx_sort_swap_avx2, xx_memory_copy_avx2, xx_sort_context_avx2, xx_sort_quick_avx2, xx_sort_stable_avx2};

    /* Query CPU policy once per sort, never once per comparison or swap.
     * Small elements retain the scalar path; wider records amortize SIMD. */
    if ((nSize >= 64) && xx_is_avx2_enabled()) {
        return &avx2;
    }
    if ((nSize >= 16) && xx_is_sse2_enabled()) {
        return &sse2;
    }
#endif
#ifdef XX_SORT_ARM_NEON
    static const xx_sort_backend neon = {xx_sort_swap_neon, xx_memory_copy_neon, xx_sort_context_neon, xx_sort_quick_neon, xx_sort_stable_neon};

    if ((nSize >= 16) && xx_is_neon_enabled()) {
        return &neon;
    }
#endif
#if !defined(XX_SORT_X86) && !defined(XX_SORT_ARM_NEON)
    (void)nSize;
#endif
    return &scalar;
}

xx_sort_movement xx_sort_select_movement(size_t nSize)
{
    const xx_sort_backend *backend = xx_sort_select_backend(nSize);
    xx_sort_movement movement;

    movement.swap = backend->fnSwap;
    movement.copy = backend->fnCopy;
    return movement;
}

void xx_sort_context(void *pBase, size_t nCount, size_t nSize, xx_sort_compare_context_fn fnCompare, void *pContext)
{
    if ((pBase == NULL) || (fnCompare == NULL) || (nCount < 2) || (nSize == 0) || (nCount > ((size_t)-1) / nSize)) {
        return;
    }
    xx_sort_select_backend(nSize)->fnContext(pBase, nCount, nSize, fnCompare, pContext);
}

void xx_sort_quick(void *pBase, size_t nCount, size_t nSize, xx_sort_compare_fn fnCompare)
{
    if ((pBase == NULL) || (fnCompare == NULL) || (nCount < 2) || (nSize == 0) || (nCount > ((size_t)-1) / nSize) || (nCount * nSize > (size_t)PTRDIFF_MAX)) {
        return;
    }
    xx_sort_select_backend(nSize)->fnQuick(pBase, nCount, nSize, fnCompare);
}

void xx_sort_stable(void *pBase, size_t nCount, size_t nSize, xx_sort_compare_fn fnCompare)
{
    if ((pBase == NULL) || (fnCompare == NULL) || (nCount < 2) || (nSize == 0) || (nCount > ((size_t)-1) / nSize)) {
        return;
    }
    xx_sort_select_backend(nSize)->fnStable(pBase, nCount, nSize, fnCompare);
}

static void xx_sort_stable_insertion(char *pBase, size_t nCount, size_t nSize, xx_sort_callback fnCompare, void *pContext)
{
    size_t i;
    for (i = 1; i < nCount; i++) {
        size_t j = i;
        while (j > 0 && fnCompare(pBase + (j - 1) * nSize, pBase + j * nSize, pContext) > 0) {
            xx_sort_swap_bytes(pBase + (j - 1) * nSize, pBase + j * nSize, nSize);
            j--;
        }
    }
}

static void xx_sort_merge_range(char *pBase, char *pTemp, size_t nCount, size_t nSize, xx_sort_callback fnCompare, void *pContext)
{
    size_t mid, i, j, k;
    if (nCount <= 16) {
        xx_sort_stable_insertion(pBase, nCount, nSize, fnCompare, pContext);
        return;
    }
    mid = nCount / 2;
    xx_sort_merge_range(pBase, pTemp, mid, nSize, fnCompare, pContext);
    xx_sort_merge_range(pBase + mid * nSize, pTemp, nCount - mid, nSize, fnCompare, pContext);

    i = 0;
    j = mid;
    k = 0;
    while (i < mid && j < nCount) {
        if (fnCompare(pBase + i * nSize, pBase + j * nSize, pContext) <= 0) {
            xx_rt_memcpy(pTemp + k * nSize, pBase + i * nSize, nSize);
            i++;
        } else {
            xx_rt_memcpy(pTemp + k * nSize, pBase + j * nSize, nSize);
            j++;
        }
        k++;
    }
    while (i < mid) {
        xx_rt_memcpy(pTemp + k * nSize, pBase + i * nSize, nSize);
        i++;
        k++;
    }
    while (j < nCount) {
        xx_rt_memcpy(pTemp + k * nSize, pBase + j * nSize, nSize);
        j++;
        k++;
    }
    xx_rt_memcpy(pBase, pTemp, nCount * nSize);
}

void xx_sort_stable_context(void *pBase, size_t nCount, size_t nSize, xx_sort_callback fnCompare, void *pContext)
{
    char *pTemp;
    if ((pBase == NULL) || (fnCompare == NULL) || (nCount < 2) || (nSize == 0) || (nCount > ((size_t)-1) / nSize)) {
        return;
    }
    pTemp = (char *)xx_rt_malloc(nCount * nSize);
    if (pTemp != NULL) {
        xx_sort_merge_range((char *)pBase, pTemp, nCount, nSize, fnCompare, pContext);
        xx_rt_free(pTemp);
    } else {
        xx_sort_stable_insertion((char *)pBase, nCount, nSize, fnCompare, pContext);
    }
}

void xx_sort_quick_context(void *pBase, size_t nCount, size_t nSize, xx_sort_callback fnCompare, void *pContext)
{
    xx_sort_context(pBase, nCount, nSize, fnCompare, pContext);
}

/* Runtime compatibility spellings retain their existing ABI while the sort
 * subsystem owns both the algorithms and their platform boundary. */
void xx_rt_qsort(void *pBase, size_t nCount, size_t nSize, xx_rt_compare_fn fnCompare)
{
    xx_sort(pBase, nCount, nSize, fnCompare);
}

void xx_rt_qsort_context(void *pBase, size_t nCount, size_t nSize, xx_rt_compare_context_fn fnCompare, void *pContext)
{
    xx_sort_context(pBase, nCount, nSize, fnCompare, pContext);
}
