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
 * @file xx_string_platform.h
 * @brief Internal platform interface for string encoding conversions.
 */

#ifndef XX_STRING_PLATFORM_H
#define XX_STRING_PLATFORM_H

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_CODEPAGE_ANSI 0
#define XX_CODEPAGE_UTF8 65001

/**
 * @brief Convert multi-byte (ANSI or UTF-8) to wide/Unicode string using platform APIs.
 */
wchar_t* xx_string_platform_mb_to_wide(const char *str, unsigned int codepage);

/**
 * @brief Convert wide/Unicode string to multi-byte (ANSI or UTF-8) using platform APIs.
 */
char* xx_string_platform_wide_to_mb(const wchar_t *wstr, unsigned int codepage);

#ifdef __cplusplus
}
#endif

#endif /* XX_STRING_PLATFORM_H */
