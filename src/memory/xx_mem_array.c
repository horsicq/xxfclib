/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/memory/xx_mem_array.h"

#include "xxfclib/memory/xx_memory.h"

bool xx_size_mul(size_t a, size_t b, size_t *out)
{
    if (!out) {
        return false;
    }

    if (b && (a > ((size_t)-1) / b)) {
        return false;
    }

    *out = a * b;

    return true;
}

bool xx_size_add(size_t a, size_t b, size_t *out)
{
    if (!out) {
        return false;
    }

    if (a > ((size_t)-1) - b) {
        return false;
    }

    *out = a + b;

    return true;
}

void *xx_mem_alloc_array(size_t count, size_t size)
{
    size_t bytes;

    if (!xx_size_mul(count, size, &bytes)) {
        return NULL;
    }

    return xx_mem_alloc(bytes ? bytes : 1);
}

void *xx_mem_realloc_array(void *pointer, size_t count, size_t size)
{
    size_t bytes;

    if (!xx_size_mul(count, size, &bytes)) {
        return NULL;
    }

    return xx_mem_realloc(pointer, bytes ? bytes : 1);
}
