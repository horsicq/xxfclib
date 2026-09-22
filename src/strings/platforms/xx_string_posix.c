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
#include <stdlib.h>
#include <string.h>

wchar_t* xx_string_platform_mb_to_wide(const char *str, unsigned int codepage) {
    (void)codepage;
    if (!str) {
        return NULL;
    }

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
    (void)codepage;
    if (!wstr) {
        return NULL;
    }

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
