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
 * @file xx_memory.c
 * @brief Memory management subsystem implementation.
 */

#include "xxfclib/memory/xx_memory.h"
#include "platforms/xx_memory_platform.h"

void* xx_mem_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    return xx_memory_platform_alloc(size);
}

void* xx_mem_calloc(size_t count, size_t size) {
    if (count == 0 || size == 0) {
        return NULL;
    }
    return xx_memory_platform_calloc(count, size);
}

void* xx_mem_realloc(void *ptr, size_t new_size) {
    return xx_memory_platform_realloc(ptr, new_size);
}

void xx_mem_free(void *ptr) {
    xx_memory_platform_free(ptr);
}

void* xx_mem_aligned_alloc(size_t alignment, size_t size) {
    if (size == 0) {
        return NULL;
    }

    /* Alignment must be a power of two */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    /* Minimum alignment is pointer size */
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    /* Calculate total size needed: size + alignment - 1 + sizeof(void*) */
    if (size > (size_t)-1 - alignment - sizeof(void*)) {
        return NULL; /* Overflow protection */
    }
    size_t total_size = size + alignment - 1 + sizeof(void*);

    void *raw = xx_mem_alloc(total_size);
    if (!raw) {
        return NULL;
    }

    uintptr_t raw_addr = (uintptr_t)raw + sizeof(void*);
    uintptr_t aligned_addr = (raw_addr + (uintptr_t)(alignment - 1)) & ~(uintptr_t)(alignment - 1);

    ((void**)aligned_addr)[-1] = raw;
    return (void*)aligned_addr;
}

void xx_mem_aligned_free(void *ptr) {
    if (!ptr) {
        return;
    }
    void *raw = ((void**)ptr)[-1];
    xx_mem_free(raw);
}

size_t xx_mem_usable_size(void *ptr) {
    if (!ptr) {
        return 0;
    }
    return xx_memory_platform_usable_size(ptr);
}

void* xx_mem_zero(void *ptr, size_t size) {
    if (!ptr || size == 0) {
        return ptr;
    }
    volatile uint8_t *p = (volatile uint8_t*)ptr;
    while (size--) {
        *p++ = 0;
    }
    return ptr;
}

void* xx_mem_copy(void *dst, const void *src, size_t size) {
    if (!dst || !src || size == 0 || dst == src) {
        return dst;
    }
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < size; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void* xx_mem_move(void *dst, const void *src, size_t size) {
    if (!dst || !src || size == 0 || dst == src) {
        return dst;
    }
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < size; ++i) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = size; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

int xx_mem_compare(const void *a, const void *b, size_t size) {
    if (a == b || size == 0) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    const uint8_t *p1 = (const uint8_t*)a;
    const uint8_t *p2 = (const uint8_t*)b;
    for (size_t i = 0; i < size; ++i) {
        if (p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }
    return 0;
}
