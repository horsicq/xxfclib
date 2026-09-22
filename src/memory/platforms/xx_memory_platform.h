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

#ifdef __cplusplus
}
#endif

#endif /* XX_MEMORY_PLATFORM_H */
