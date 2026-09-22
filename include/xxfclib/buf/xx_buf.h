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
 * @file xx_buf.h
 * @brief Growable byte buffer.
 *
 * This is the binary-safe counterpart to xx_str_a_t. The distinction matters:
 *
 *   - xx_str_a_t holds a C string. Its length is whatever strlen says, so a
 *     zero byte ends the content.
 *   - xx_buf_t holds BYTES. @ref xx_buf_t::size is authoritative, the content
 *     may contain any number of zero bytes, and appending '\\0' appends one
 *     byte like any other.
 *
 * Anything assembling data whose bytes come from a file, a decoder or a
 * script - where a zero is a value rather than a terminator - wants this type.
 * Reaching for the string builder there truncates at the first zero byte and
 * the loss is silent.
 *
 * As a convenience the buffer always keeps a zero byte at data[size] that is
 * not counted in the size, so a buffer that happens to hold text can be passed
 * to a C string API without copying.
 *
 * @par Failure handling
 * Allocation failure raises a sticky flag rather than aborting or being
 * reported once and forgotten. Every later append is a no-op, @ref xx_buf_ok
 * stays false, and @ref xx_buf_detach yields NULL. A caller may therefore
 * append freely and check once at the end: a partially built buffer can never
 * be mistaken for a complete one, which a per-call bool that callers routinely
 * discard does not give you.
 *
 * @par Provenance
 * The growth policy and the detach contract come from cdie's CDBuf
 * (github.com/horsicq, MIT). The sticky-failure behaviour replaces CDBuf's
 * abort-the-process allocator policy, which its own author notes is "the right
 * answer for a console tool and the wrong one" inside a library.
 */

#ifndef XXFCLIB_XX_BUF_H
#define XXFCLIB_XX_BUF_H

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Growable byte buffer. Initialise with @ref xx_buf_init.
 */
typedef struct xx_buf_s {
    char  *data;     /**< Bytes; NULL until the first append. Never counted past size. */
    size_t size;     /**< Number of bytes held. Authoritative; zero bytes count. */
    size_t capacity; /**< Bytes allocated, including room for the trailing zero. */
    bool   failed;   /**< Sticky: set on allocation failure, never cleared except by init/free. */
} xx_buf_t;

/** @brief Put @p buf into the empty state. Allocates nothing. */
XXFC_API void xx_buf_init(xx_buf_t *buf);

/** @brief Release the contents and return @p buf to the empty state. */
XXFC_API void xx_buf_free(xx_buf_t *buf);

/**
 * @brief Make room for at least @p capacity bytes plus the trailing zero.
 * @return false if the allocation failed, which also raises the sticky flag.
 */
XXFC_API bool xx_buf_reserve(xx_buf_t *buf, size_t capacity);

/** @brief Drop the contents, keeping the allocation and the sticky flag. */
XXFC_API void xx_buf_clear(xx_buf_t *buf);

/**
 * @brief Append @p size bytes. Binary-safe: @p data may contain zero bytes.
 * @return false if the buffer had already failed or this append failed.
 */
XXFC_API bool xx_buf_append(xx_buf_t *buf, const void *data, size_t size);

/**
 * @brief Append a C string, excluding its terminator. NULL appends nothing.
 */
XXFC_API bool xx_buf_append_str(xx_buf_t *buf, const char *str);

/**
 * @brief Append one byte.
 *
 * Appending '\\0' appends a zero byte and advances the size, which is the
 * whole point of this type over the string builder.
 */
XXFC_API bool xx_buf_append_char(xx_buf_t *buf, char c);

/**
 * @brief Append printf-formatted text.
 *
 * Formatting goes through xx_rt_vsnprintf, so it is available in a build with
 * no C runtime. Note that the runtime layer's formatter has no floating-point
 * conversions - use the xx_rt_dtoa_* family for those.
 */
XXFC_API XX_RT_PRINTF_LIKE(2, 3) bool xx_buf_appendf(xx_buf_t *buf,
                                                     const char *fmt, ...);

/** @brief False once any operation on @p buf has failed. */
XXFC_API bool xx_buf_ok(const xx_buf_t *buf);

/**
 * @brief Hand ownership of the bytes to the caller and reset @p buf.
 *
 * Returns a non-NULL, zero-terminated allocation even when the buffer is
 * empty, so the result is always safe to free and to treat as a C string.
 * @p size receives the byte count, which excludes that terminator.
 *
 * @return NULL if the buffer has failed - the contents are incomplete and are
 *         released rather than handed over. Free the result with xx_rt_free.
 */
XXFC_API char *xx_buf_detach(xx_buf_t *buf, size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_BUF_H */
