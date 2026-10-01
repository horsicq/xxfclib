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
#include "xxfclib/rt/xx_rt.h"
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


/* Runtime allocation remains platform-specific. Its zero-size behavior is
 * separate from xx_memory_platform_* allocation. Memory operations themselves
 * are shared in src/memory/xx_memory_rt.c. */

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
