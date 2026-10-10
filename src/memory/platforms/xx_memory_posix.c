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
#include "xxfclib/rt/xx_rt.h"
#include <stdlib.h>

void *xx_memory_platform_alloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }
    return malloc(size);
}

void *xx_memory_platform_calloc(size_t count, size_t size)
{
    if (count == 0 || size == 0) {
        return NULL;
    }
    return calloc(count, size);
}

void *xx_memory_platform_realloc(void *ptr, size_t new_size)
{
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

void xx_memory_platform_free(void *ptr)
{
    if (ptr) {
        free(ptr);
    }
}

size_t xx_memory_platform_usable_size(void *ptr)
{
    (void)ptr;
    return 0;
}

/* Runtime allocation remains platform-specific. Its zero-size behavior is
 * separate from xx_memory_platform_* allocation. Memory operations themselves
 * are shared in src/memory/xx_memory_rt.c. */

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
