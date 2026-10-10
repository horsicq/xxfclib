/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"
#include "platforms/xx_memory_platform.h"

/* Volatile byte tails prevent loop-to-CRT rewrites and stay strictly bounded. */
void xx_memory_move_scalar(uint8_t *destination, const uint8_t *source, size_t size)
{
    volatile uint8_t *dst = destination;
    const volatile uint8_t *src = source;
    uintptr_t d = (uintptr_t)destination, s = (uintptr_t)source;
    if (d == s || size == 0) return;
    if (d > s && d - s < size) {
        while (size >= 8) {
            size -= 8;
            xx_memory_store_word(destination + size, xx_memory_load_word(source + size));
        }
        while (size != 0) {
            --size;
            dst[size] = src[size];
        }
    } else {
        size_t at = 0;
        for (; size - at >= 8; at += 8) xx_memory_store_word(destination + at, xx_memory_load_word(source + at));
        for (; at < size; ++at) dst[at] = src[at];
    }
}

void xx_memory_set_scalar(uint8_t *destination, uint8_t value, size_t size)
{
    volatile uint8_t *dst = destination;
    uint64_t pattern = (uint64_t)value * UINT64_C(0x0101010101010101);
    size_t at = 0;
    for (; size - at >= 8; at += 8) xx_memory_store_word(destination + at, pattern);
    for (; at < size; ++at) dst[at] = value;
}

int xx_memory_compare_scalar(const uint8_t *first, const uint8_t *second, size_t size)
{
    const volatile uint8_t *a = first, *b = second;
    size_t at = 0;
    for (; size - at >= 8; at += 8)
        if (xx_memory_load_word(first + at) != xx_memory_load_word(second + at)) break;
    for (; at < size; ++at) {
        uint8_t left = a[at], right = b[at];
        if (left != right) return (int)left - (int)right;
    }
    return 0;
}

const uint8_t *xx_memory_find_scalar(const uint8_t *source, uint8_t value, size_t size)
{
    const volatile uint8_t *src = source;
    for (size_t at = 0; at < size; ++at)
        if (src[at] == value) return source + at;
    return NULL;
}

typedef struct xx_memory_operations_s {
    void (*copy)(uint8_t *, const uint8_t *, size_t);
    void (*move)(uint8_t *, const uint8_t *, size_t);
    void (*set)(uint8_t *, uint8_t, size_t);
    int (*compare)(const uint8_t *, const uint8_t *, size_t);
    const uint8_t *(*find)(const uint8_t *, uint8_t, size_t);
} xx_memory_operations;

static const xx_memory_operations *xx_memory_operations_for_size(size_t size)
{
    static const xx_memory_operations scalar = {xx_memory_copy_scalar, xx_memory_move_scalar, xx_memory_set_scalar, xx_memory_compare_scalar, xx_memory_find_scalar};
    static const xx_memory_operations sse2 = {xx_memory_copy_sse2, xx_memory_move_sse2, xx_memory_set_sse2, xx_memory_compare_sse2, xx_memory_find_sse2};
    static const xx_memory_operations avx2 = {xx_memory_copy_avx2, xx_memory_move_avx2, xx_memory_set_avx2, xx_memory_compare_avx2, xx_memory_find_avx2};
    /* Avoid feature-query overhead for short fields and compiler-generated
     * initialization during feature detection. Honor changes to the switches. */
    if (size >= 32) {
        if (xx_is_avx2_enabled()) return &avx2;
        if (xx_is_sse2_enabled()) return &sse2;
    }
    return &scalar;
}

void *xx_rt_memcpy(void *destination, const void *source, size_t size)
{
    if (size != 0 && destination != source) xx_memory_operations_for_size(size)->copy((uint8_t *)destination, (const uint8_t *)source, size);
    return destination;
}

void *xx_rt_memmove(void *destination, const void *source, size_t size)
{
    if (size != 0 && destination != source) xx_memory_operations_for_size(size)->move((uint8_t *)destination, (const uint8_t *)source, size);
    return destination;
}

void *xx_rt_memset(void *destination, int value, size_t size)
{
    if (size != 0) xx_memory_operations_for_size(size)->set((uint8_t *)destination, (uint8_t)value, size);
    return destination;
}

int xx_rt_memcmp(const void *first, const void *second, size_t size)
{
    if (size == 0 || first == second) return 0;
    return xx_memory_operations_for_size(size)->compare((const uint8_t *)first, (const uint8_t *)second, size);
}

void *xx_rt_memchr(const void *source, int value, size_t size)
{
    if (size == 0) return NULL;
    return (void *)xx_memory_operations_for_size(size)->find((const uint8_t *)source, (uint8_t)value, size);
}
