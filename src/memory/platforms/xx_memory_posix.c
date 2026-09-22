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
 * @file xx_memory_posix.c
 * @brief Standard POSIX/C memory management implementation.
 */

#if !defined(_WIN32)

#include "xx_memory_platform.h"
#include <stdlib.h>

void* xx_memory_platform_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    return malloc(size);
}

void* xx_memory_platform_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return NULL;
    }
    return calloc(count, size);
}

void* xx_memory_platform_realloc(void *ptr, size_t new_size) {
    if (!ptr) {
        if (new_size == 0) {
            return NULL;
        }
        return malloc(new_size);
    }
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

void xx_memory_platform_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}

size_t xx_memory_platform_usable_size(void *ptr) {
    (void)ptr;
    return 0;
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
#include <windows.h>
#else
#include <stdlib.h>
#include <string.h>
#endif


/* ------------------------------------------------------------------------ */
/*  CRT-compatible Runtime Memory Primitives                                */
/* ------------------------------------------------------------------------ */


void *xx_rt_memcpy(void *pDestination, const void *pSource, size_t nSize)
{
    return memcpy(pDestination, pSource, nSize);
}

void *xx_rt_memmove(void *pDestination, const void *pSource, size_t nSize)
{
    return memmove(pDestination, pSource, nSize);
}

void *xx_rt_memset(void *pDestination, int nValue, size_t nSize)
{
    return memset(pDestination, nValue, nSize);
}

int xx_rt_memcmp(const void *pLeft, const void *pRight, size_t nSize)
{
    return memcmp(pLeft, pRight, nSize);
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


void *xx_rt_malloc(size_t nSize)
{
    return malloc(nSize);
}

void *xx_rt_calloc(size_t nCount, size_t nSize)
{
    return calloc(nCount, nSize);
}

void *xx_rt_realloc(void *pPtr, size_t nSize)
{
    return realloc(pPtr, nSize);
}

void xx_rt_free(void *pPtr)
{
    free(pPtr);
}

#endif /* !_WIN32 */
