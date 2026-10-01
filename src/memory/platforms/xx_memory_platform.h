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
 * @file xx_memory_platform.h
 * @brief Internal platform interface for memory management operations.
 */

#ifndef XX_MEMORY_PLATFORM_H
#define XX_MEMORY_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocate memory on host platform.
 */
void* xx_memory_platform_alloc(size_t size);

/**
 * @brief Allocate zeroed memory on host platform.
 */
void* xx_memory_platform_calloc(size_t count, size_t size);

/**
 * @brief Reallocate memory on host platform.
 */
void* xx_memory_platform_realloc(void *ptr, size_t new_size);

/**
 * @brief Free memory on host platform.
 */
void xx_memory_platform_free(void *ptr);

/**
 * @brief Query usable size of allocated block on host platform.
 */
size_t xx_memory_platform_usable_size(void *ptr);

/* Compiler-specific unaligned, alias-safe word access for the scalar kernels.
 * Keep these details here, with the other platform/compiler adaptations.
 * Volatile word stores prevent loop-to-memcpy/memset transformations. */
#if defined(__GNUC__) || defined(__clang__)
typedef struct __attribute__((packed, may_alias)) xx_memory_unaligned_word_s {
    uint64_t value;
} xx_memory_unaligned_word;
#elif defined(_MSC_VER)
/* MSVC supports aliasing through packed objects and does not apply GCC-style
 * strict-aliasing optimizations. Packing also permits unaligned word access. */
#pragma pack(push, 1)
typedef struct xx_memory_unaligned_word_s { uint64_t value; } xx_memory_unaligned_word;
#pragma pack(pop)
#endif

static inline uint64_t xx_memory_load_word(const uint8_t *source) {
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
    return ((const xx_memory_unaligned_word *)(const void *)source)->value;
#else
    union { uint64_t value; uint8_t bytes[8]; } word;
    word.bytes[0] = source[0]; word.bytes[1] = source[1];
    word.bytes[2] = source[2]; word.bytes[3] = source[3];
    word.bytes[4] = source[4]; word.bytes[5] = source[5];
    word.bytes[6] = source[6]; word.bytes[7] = source[7];
    return word.value;
#endif
}

static inline void xx_memory_store_word(uint8_t *destination, uint64_t value) {
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
    ((volatile xx_memory_unaligned_word *)(void *)destination)->value = value;
#else
    union { uint64_t value; uint8_t bytes[8]; } word;
    volatile uint8_t *dst = destination;
    word.value = value;
    dst[0] = word.bytes[0]; dst[1] = word.bytes[1];
    dst[2] = word.bytes[2]; dst[3] = word.bytes[3];
    dst[4] = word.bytes[4]; dst[5] = word.bytes[5];
    dst[6] = word.bytes[6]; dst[7] = word.bytes[7];
#endif
}

/* Shared bounded operations, including unaligned extents and allocation/page
 * ends. Copy requires nonoverlapping buffers; move accepts overlap. Compare
 * orders unsigned bytes at the first difference, and find returns the first
 * matching byte. SIMD callers must check the corresponding CPU/OS feature
 * first; on other architectures these use the scalar fallback. */
void xx_memory_copy_scalar(uint8_t *destination, const uint8_t *source, size_t size);
void xx_memory_copy_sse2(uint8_t *destination, const uint8_t *source, size_t size);
void xx_memory_copy_avx2(uint8_t *destination, const uint8_t *source, size_t size);

void xx_memory_move_scalar(uint8_t *, const uint8_t *, size_t);
void xx_memory_set_scalar(uint8_t *, uint8_t, size_t);
int xx_memory_compare_scalar(const uint8_t *, const uint8_t *, size_t);
const uint8_t *xx_memory_find_scalar(const uint8_t *, uint8_t, size_t);
void xx_memory_move_sse2(uint8_t *, const uint8_t *, size_t);
void xx_memory_set_sse2(uint8_t *, uint8_t, size_t);
int xx_memory_compare_sse2(const uint8_t *, const uint8_t *, size_t);
const uint8_t *xx_memory_find_sse2(const uint8_t *, uint8_t, size_t);
void xx_memory_move_avx2(uint8_t *, const uint8_t *, size_t);
void xx_memory_set_avx2(uint8_t *, uint8_t, size_t);
int xx_memory_compare_avx2(const uint8_t *, const uint8_t *, size_t);
const uint8_t *xx_memory_find_avx2(const uint8_t *, uint8_t, size_t);

#ifdef __cplusplus
}
#endif

#endif /* XX_MEMORY_PLATFORM_H */
