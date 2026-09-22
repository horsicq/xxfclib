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

/**
 * @file xx_memory_windows.c
 * @brief Windows platform memory management implementation using pure Win32 API.
 */

#if defined(_WIN32)

#include "xx_memory_platform.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void* xx_memory_platform_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    HANDLE hHeap = GetProcessHeap();
    if (!hHeap) {
        return NULL;
    }

    return HeapAlloc(hHeap, 0, (SIZE_T)size);
}

void* xx_memory_platform_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return NULL;
    }

    /* Check for integer multiplication overflow */
    if (size > (size_t)-1 / count) {
        return NULL;
    }

    SIZE_T total = (SIZE_T)(count * size);
    HANDLE hHeap = GetProcessHeap();
    if (!hHeap) {
        return NULL;
    }

    return HeapAlloc(hHeap, HEAP_ZERO_MEMORY, total);
}

void* xx_memory_platform_realloc(void *ptr, size_t new_size) {
    HANDLE hHeap = GetProcessHeap();
    if (!hHeap) {
        return NULL;
    }

    if (!ptr) {
        if (new_size == 0) {
            return NULL;
        }
        return HeapAlloc(hHeap, 0, (SIZE_T)new_size);
    }

    if (new_size == 0) {
        HeapFree(hHeap, 0, ptr);
        return NULL;
    }

    return HeapReAlloc(hHeap, 0, ptr, (SIZE_T)new_size);
}

void xx_memory_platform_free(void *ptr) {
    if (!ptr) {
        return;
    }

    HANDLE hHeap = GetProcessHeap();
    if (hHeap) {
        HeapFree(hHeap, 0, ptr);
    }
}

size_t xx_memory_platform_usable_size(void *ptr) {
    if (!ptr) {
        return 0;
    }

    HANDLE hHeap = GetProcessHeap();
    if (!hHeap) {
        return 0;
    }

    SIZE_T sz = HeapSize(hHeap, 0, ptr);
    if (sz == (SIZE_T)-1) {
        return 0;
    }

    return (size_t)sz;
}


/* ------------------------------------------------------------------------ */
/*  Runtime memory primitives                                               */
/* ------------------------------------------------------------------------ */
/* These define the public xx_rt_mem and xx_rt_malloc families directly, with no
 * wrapper layer - the same shape xx_rt_utf8_to_utf16 uses in the string
 * platform files. They are deliberately NOT folded into xx_memory_platform_*:
 * the contracts differ, xx_rt_malloc rounds a zero-byte request up to one byte
 * and xx_rt_realloc(ptr, 0) keeps the block, where the xx_memory_platform_*
 * pair returns NULL for both. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdlib.h>
#include <string.h>
#endif

#if defined(_MSC_VER)
/* Stop the compiler from rewriting the loops below into calls to itself. */
#pragma function(memset, memcpy)
#endif

/* ------------------------------------------------------------------------ */
/*  CRT-compatible Runtime Memory Primitives                                */
/* ------------------------------------------------------------------------ */

/* The Rtl*Memory names in winnt.h are macros that expand back to the CRT, so
 * they would not remove the dependency. These copy a machine word at a time
 * to stay close to the CRT in the hot paths (signature compare, string
 * concatenation).                                                           */
typedef size_t xx_rt_word;

void *xx_rt_memcpy(void *pDestination, const void *pSource, size_t nSize)
{
    unsigned char *pDst = (unsigned char *)pDestination;
    const unsigned char *pSrc = (const unsigned char *)pSource;

    while (nSize >= sizeof(xx_rt_word)) {
        *(xx_rt_word *)pDst = *(const xx_rt_word *)pSrc;
        pDst += sizeof(xx_rt_word);
        pSrc += sizeof(xx_rt_word);
        nSize -= sizeof(xx_rt_word);
    }

    while (nSize--) {
        *pDst++ = *pSrc++;
    }

    return pDestination;
}

void *xx_rt_memmove(void *pDestination, const void *pSource, size_t nSize)
{
    unsigned char *pDst = (unsigned char *)pDestination;
    const unsigned char *pSrc = (const unsigned char *)pSource;

    if (pDst == pSrc) {
        return pDestination;
    }

    if ((pDst < pSrc) || (pDst >= pSrc + nSize)) {
        return xx_rt_memcpy(pDestination, pSource, nSize);
    }

    /* Overlapping and moving forward: copy backwards. */
    pDst += nSize;
    pSrc += nSize;

    while (nSize--) {
        *--pDst = *--pSrc;
    }

    return pDestination;
}

void *xx_rt_memset(void *pDestination, int nValue, size_t nSize)
{
    unsigned char *pDst = (unsigned char *)pDestination;
    unsigned char nByte = (unsigned char)nValue;
    xx_rt_word nPattern = 0;
    size_t i = 0;

    for (i = 0; i < sizeof(xx_rt_word); i++) {
        nPattern = (nPattern << 8) | nByte;
    }

    while (nSize >= sizeof(xx_rt_word)) {
        *(xx_rt_word *)pDst = nPattern;
        pDst += sizeof(xx_rt_word);
        nSize -= sizeof(xx_rt_word);
    }

    while (nSize--) {
        *pDst++ = nByte;
    }

    return pDestination;
}

int xx_rt_memcmp(const void *pLeft, const void *pRight, size_t nSize)
{
    const unsigned char *pA = (const unsigned char *)pLeft;
    const unsigned char *pB = (const unsigned char *)pRight;

    /* RtlCompareMemory returns the count of equal bytes, not an ordering,
     * so it cannot stand in for memcmp.                                     */
    while (nSize >= sizeof(xx_rt_word)) {
        if (*(const xx_rt_word *)pA != *(const xx_rt_word *)pB) {
            break;
        }

        pA += sizeof(xx_rt_word);
        pB += sizeof(xx_rt_word);
        nSize -= sizeof(xx_rt_word);
    }

    while (nSize--) {
        if (*pA != *pB) {
            return (int)*pA - (int)*pB;
        }

        pA++;
        pB++;
    }

    return 0;
}

void *xx_rt_memchr(const void *pMemory, int nChar, size_t nSize)
{
    const unsigned char *p = (const unsigned char *)pMemory;
    unsigned char nWanted = (unsigned char)nChar;
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        if (p[i] == nWanted) {
            return (void *)(p + i);
        }
    }

    return NULL;
}

static HANDLE xx_rt_process_heap(void)
{
    static HANDLE hHeap = NULL;

    if (hHeap == NULL) {
        hHeap = GetProcessHeap();
    }

    return hHeap;
}

void *xx_rt_malloc(size_t nSize)
{
    return HeapAlloc(xx_rt_process_heap(), 0, nSize ? nSize : 1);
}

void *xx_rt_calloc(size_t nCount, size_t nSize)
{
    size_t nTotal = nCount * nSize;

    /* Reject a multiplication overflow rather than under-allocating. */
    if (nCount && ((nTotal / nCount) != nSize)) {
        return NULL;
    }

    return HeapAlloc(xx_rt_process_heap(), HEAP_ZERO_MEMORY, nTotal ? nTotal : 1);
}

void *xx_rt_realloc(void *pPtr, size_t nSize)
{
    if (pPtr == NULL) {
        return xx_rt_malloc(nSize);
    }

    return HeapReAlloc(xx_rt_process_heap(), 0, pPtr, nSize ? nSize : 1);
}

void xx_rt_free(void *pPtr)
{
    if (pPtr) {
        HeapFree(xx_rt_process_heap(), 0, pPtr);
    }
}

#endif /* _WIN32 */
