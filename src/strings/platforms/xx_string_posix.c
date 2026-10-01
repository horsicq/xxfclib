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
 * @file xx_string_posix.c
 * @brief POSIX platform string conversions fallback.
 */

#if !defined(_WIN32)

#include "xx_string_platform.h"

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Explicit UTF-8 conversions must not depend on the process locale.  A
 * command-line program need not call setlocale(), even when its paths are
 * UTF-8.  wchar_t is a Unicode scalar on the supported POSIX platforms. */
static wchar_t *xx_string_utf8_to_wide(const char *str) {
    const unsigned char *read = (const unsigned char *)str;
    size_t length = strlen(str);
    wchar_t *result;
    wchar_t *write;

    if (length > SIZE_MAX / sizeof(wchar_t) - 1U) return NULL;
    result = (wchar_t *)xx_mem_alloc((length + 1U) * sizeof(wchar_t));
    if (!result) return NULL;
    write = result;
    while (*read) {
        uint32_t code;
        unsigned int more;
        unsigned int i;
        unsigned char first = *read++;
        if (first < 0x80U) { code = first; more = 0U; }
        else if (first >= 0xC2U && first <= 0xDFU) {
            code = first & 0x1FU; more = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            code = first & 0x0FU; more = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            code = first & 0x07U; more = 3U;
        } else goto invalid;
        for (i = 0U; i < more; ++i) {
            unsigned char next = *read;
            if ((next & 0xC0U) != 0x80U) goto invalid;
            code = (code << 6) | (next & 0x3FU);
            ++read;
        }
        if ((more == 1U && code < 0x80U) ||
            (more == 2U && code < 0x800U) ||
            (more == 3U && code < 0x10000U) ||
            (code >= 0xD800U && code <= 0xDFFFU) ||
            code > 0x10FFFFU || code > (uint32_t)WCHAR_MAX) goto invalid;
        *write++ = (wchar_t)code;
    }
    *write = L'\0';
    return result;
invalid:
    xx_mem_free(result);
    return NULL;
}

static char *xx_string_wide_to_utf8(const wchar_t *str) {
    size_t length = wcslen(str);
    size_t i;
    char *result;
    unsigned char *write;
    if (length > (SIZE_MAX - 1U) / 4U) return NULL;
    result = (char *)xx_mem_alloc(length * 4U + 1U);
    if (!result) return NULL;
    write = (unsigned char *)result;
    for (i = 0U; i < length; ++i) {
        uint32_t code = (uint32_t)str[i];
        if ((code >= 0xD800U && code <= 0xDFFFU) || code > 0x10FFFFU)
            goto invalid;
        if (code < 0x80U) *write++ = (unsigned char)code;
        else if (code < 0x800U) {
            *write++ = (unsigned char)(0xC0U | (code >> 6));
            *write++ = (unsigned char)(0x80U | (code & 0x3FU));
        } else if (code < 0x10000U) {
            *write++ = (unsigned char)(0xE0U | (code >> 12));
            *write++ = (unsigned char)(0x80U | ((code >> 6) & 0x3FU));
            *write++ = (unsigned char)(0x80U | (code & 0x3FU));
        } else {
            *write++ = (unsigned char)(0xF0U | (code >> 18));
            *write++ = (unsigned char)(0x80U | ((code >> 12) & 0x3FU));
            *write++ = (unsigned char)(0x80U | ((code >> 6) & 0x3FU));
            *write++ = (unsigned char)(0x80U | (code & 0x3FU));
        }
    }
    *write = '\0';
    return result;
invalid:
    xx_mem_free(result);
    return NULL;
}

wchar_t* xx_string_platform_mb_to_wide(const char *str, unsigned int codepage) {
    if (!str) {
        return NULL;
    }
    if (codepage == XX_CODEPAGE_UTF8) return xx_string_utf8_to_wide(str);

    size_t len = mbstowcs(NULL, str, 0);
    if (len == (size_t)-1) {
        return NULL;
    }

    wchar_t *wbuf = (wchar_t*)xx_mem_alloc((len + 1) * sizeof(wchar_t));
    if (!wbuf) {
        return NULL;
    }

    mbstowcs(wbuf, str, len + 1);
    return wbuf;
}

char* xx_string_platform_wide_to_mb(const wchar_t *wstr, unsigned int codepage) {
    if (!wstr) {
        return NULL;
    }
    if (codepage == XX_CODEPAGE_UTF8) return xx_string_wide_to_utf8(wstr);

    size_t len = wcstombs(NULL, wstr, 0);
    if (len == (size_t)-1) {
        return NULL;
    }

    char *mbuf = (char*)xx_mem_alloc(len + 1);
    if (!mbuf) {
        return NULL;
    }

    wcstombs(mbuf, wstr, len + 1);
    return mbuf;
}


/* Runtime string primitives (thin delegation to libc, which is always present here).
 * These define the public xx_rt_str* names directly, as
 * xx_rt_utf8_to_utf16 in this file already does - no wrapper layer. */
/* --- Runtime string primitives: libc delegation --- */

size_t xx_rt_strlen(const char *pString)
{
    return strlen(pString);
}

/* lstrcmpA is deliberately NOT used: it compares with the user's locale via
 * CompareStringA, so "a" vs "B" and punctuation would order differently from
 * a byte comparison. The signature database is sorted with this function and
 * JavaScript relational operators rely on it, so the ordering has to be
 * plain unsigned byte order on every platform.                              */
int xx_rt_strcmp(const char *pLeft, const char *pRight)
{
    return strcmp(pLeft, pRight);
}

int xx_rt_strncmp(const char *pLeft, const char *pRight, size_t nSize)
{
    return strncmp(pLeft, pRight, nSize);
}

/* strncpy semantics: copy at most nSize bytes, zero-pad the remainder, and do
 * not terminate when the source is longer. lstrcpynA differs on both counts,
 * so it is not used here.                                                   */
char *xx_rt_strncpy(char *pDestination, const char *pSource, size_t nSize)
{
    return strncpy(pDestination, pSource, nSize);
}

char *xx_rt_strchr(const char *pString, int nChar)
{
    return strchr(pString, nChar);
}

char *xx_rt_strrchr(const char *pString, int nChar)
{
    return strrchr(pString, nChar);
}

char *xx_rt_strstr(const char *pHaystack, const char *pNeedle)
{
    return strstr(pHaystack, pNeedle);
}

#endif /* !_WIN32 */
