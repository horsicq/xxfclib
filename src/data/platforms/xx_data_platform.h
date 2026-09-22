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
 * @file xx_data_platform.h
 * @brief Internal platform interface for SIMD-accelerated binary search operations.
 */

#ifndef XX_DATA_PLATFORM_H
#define XX_DATA_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct xx_pd_struct;
typedef struct xx_pd_struct xx_pd_struct;

/**
 * @brief Search for a byte pattern using SSE2 128-bit SIMD instructions.
 * @return Byte offset where pattern starts, or -1 if not found or stopped.
 */
int64_t xx_data_find_bytes_sse2(const uint8_t *pdata, size_t data_size, size_t start_offset, const uint8_t *pat, size_t pattern_size, xx_pd_struct *pd);

/**
 * @brief Search for a byte pattern using AVX2 256-bit SIMD instructions.
 * @return Byte offset where pattern starts, or -1 if not found or stopped.
 */
int64_t xx_data_find_bytes_avx2(const uint8_t *pdata, size_t data_size, size_t start_offset, const uint8_t *pat, size_t pattern_size, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_DATA_PLATFORM_H */
