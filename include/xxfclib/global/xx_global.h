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
 * @file xx_global.h
 * @brief Global configuration options for buffer and file buffer sizes.
 */

#ifndef XX_GLOBAL_H
#define XX_GLOBAL_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DEFAULT_BUFFER_SIZE      (64 * 1024) /* 64 KB default general memory buffer size */
#define XX_DEFAULT_FILE_BUFFER_SIZE (64 * 1024) /* 64 KB default file/device I/O buffer size */

/* --- General Buffer Size Configuration --- */

/**
 * @brief Set global general buffer size.
 * @param size Buffer size in bytes. If 0, resets to XX_DEFAULT_BUFFER_SIZE.
 */
XXFC_API void   xx_set_buffer_size(size_t size);

/**
 * @brief Get global general buffer size.
 * @return Current buffer size in bytes.
 */
XXFC_API size_t xx_get_buffer_size(void);

/* --- File / I/O Device Buffer Size Configuration --- */

/**
 * @brief Set global file/device I/O buffer size.
 * @param size File buffer size in bytes. If 0, resets to XX_DEFAULT_FILE_BUFFER_SIZE.
 */
XXFC_API void   xx_set_file_buffer_size(size_t size);

/**
 * @brief Get global file/device I/O buffer size.
 * @return Current file buffer size in bytes.
 */
XXFC_API size_t xx_get_file_buffer_size(void);

/* Convenient aliases */
XXFC_API void   xx_global_set_buffer_size(size_t size);
XXFC_API size_t xx_global_get_buffer_size(void);
XXFC_API void   xx_global_set_file_buffer_size(size_t size);
XXFC_API size_t xx_global_get_file_buffer_size(void);

#define xx_get_file_BufferSize xx_get_file_buffer_size
#define xx_set_file_BufferSize xx_set_file_buffer_size

/* ========================================================================= */
/* --- CPU Feature Detection & Configuration (SSE2 / AVX2)               --- */
/* ========================================================================= */

/**
 * @brief Check if SSE2 instruction set is supported by the CPU.
 * @return true if SSE2 is physically supported, false otherwise.
 */
XXFC_API bool xx_has_sse2(void);

/**
 * @brief Check if AVX2 instruction set is supported by both CPU and OS.
 * @return true if AVX2 is physically supported and enabled by OS, false otherwise.
 */
XXFC_API bool xx_has_avx2(void);

/**
 * @brief Enable or disable SSE2 acceleration globally.
 * @param enable true to enable (if supported by CPU), false to disable.
 */
XXFC_API void xx_set_sse2_enabled(bool enable);
XXFC_API void xx_enable_sse2(bool enable);

/**
 * @brief Check if SSE2 acceleration is currently enabled and supported.
 * @return true if SSE2 is enabled and supported, false otherwise.
 */
XXFC_API bool xx_is_sse2_enabled(void);

/**
 * @brief Enable or disable AVX2 acceleration globally.
 * @param enable true to enable (if supported by CPU/OS), false to disable.
 */
XXFC_API void xx_set_avx2_enabled(bool enable);
XXFC_API void xx_enable_avx2(bool enable);

/**
 * @brief Check if AVX2 acceleration is currently enabled and supported.
 * @return true if AVX2 is enabled and supported, false otherwise.
 */
XXFC_API bool xx_is_avx2_enabled(void);

/* Aliases with xx_global prefix */
XXFC_API bool xx_global_has_sse2(void);
XXFC_API bool xx_global_has_avx2(void);
XXFC_API void xx_global_set_sse2_enabled(bool enable);
XXFC_API void xx_global_enable_sse2(bool enable);
XXFC_API bool xx_global_is_sse2_enabled(void);
XXFC_API void xx_global_set_avx2_enabled(bool enable);
XXFC_API void xx_global_enable_avx2(bool enable);
XXFC_API bool xx_global_is_avx2_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* XX_GLOBAL_H */
