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
 * @file xx_string_windows.c
 * @brief Windows platform string conversions using pure Win32 KERNEL32 API.
 */

#if defined(_WIN32)

#include "xx_string_platform.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* TinyCC's bundled <windows.h> leaves <winnls.h> commented out, so the code
 * page conversions are missing. Declare exactly what is used; the guard keeps
 * this out of the way of MSVC / MinGW, whose headers already provide them. */
#ifndef CP_UTF8
#define CP_UTF8 65001
int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, const char *lpMultiByteStr, int cbMultiByte, wchar_t *lpWideCharStr, int cchWideChar);
int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, const wchar_t *lpWideCharStr, int cchWideChar, char *lpMultiByteStr, int cbMultiByte, const char *lpDefaultChar, int *lpUsedDefaultChar);
#endif

/* Paths inside the runtime are UTF-8. Keep the original xx_rt allocator
 * contract for these compatibility helpers: callers release their results
 * with xx_rt_free. */
void *xx_rt_utf8_to_utf16(const char *pUtf8)
{
    int nLen = 0;
    WCHAR *pResult = NULL;

    if (pUtf8 == NULL) {
        return NULL;
    }

    nLen = MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, NULL, 0);

    if (nLen <= 0) {
        return NULL;
    }

    pResult = (WCHAR *)xx_rt_malloc((size_t)nLen * sizeof(WCHAR));

    if (pResult == NULL) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, pResult, nLen) <= 0) {
        xx_rt_free(pResult);

        return NULL;
    }

    return (void *)pResult;
}

char *xx_rt_utf16_to_utf8(const void *pUtf16)
{
    const WCHAR *pWide = (const WCHAR *)pUtf16;
    int nLen = 0;
    char *pResult = NULL;

    if (pWide == NULL) {
        return NULL;
    }

    nLen = WideCharToMultiByte(CP_UTF8, 0, pWide, -1, NULL, 0, NULL, NULL);

    if (nLen <= 0) {
        return NULL;
    }

    pResult = (char *)xx_rt_malloc((size_t)nLen);

    if (pResult == NULL) {
        return NULL;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, pWide, -1, pResult, nLen, NULL, NULL) <= 0) {
        xx_rt_free(pResult);

        return NULL;
    }

    return pResult;
}

wchar_t* xx_string_platform_mb_to_wide(const char *str, unsigned int codepage) {
    if (!str) {
        return NULL;
    }

    UINT cp = (codepage == XX_CODEPAGE_UTF8) ? CP_UTF8 : CP_ACP;

    int wlen = MultiByteToWideChar(cp, 0, str, -1, NULL, 0);
    if (wlen <= 0) {
        return NULL;
    }

    wchar_t *wbuf = (wchar_t*)xx_mem_alloc((size_t)wlen * sizeof(wchar_t));
    if (!wbuf) {
        return NULL;
    }

    if (MultiByteToWideChar(cp, 0, str, -1, wbuf, wlen) <= 0) {
        xx_mem_free(wbuf);
        return NULL;
    }

    return wbuf;
}

char* xx_string_platform_wide_to_mb(const wchar_t *wstr, unsigned int codepage) {
    if (!wstr) {
        return NULL;
    }

    UINT cp = (codepage == XX_CODEPAGE_UTF8) ? CP_UTF8 : CP_ACP;

    int mlen = WideCharToMultiByte(cp, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (mlen <= 0) {
        return NULL;
    }

    char *mbuf = (char*)xx_mem_alloc((size_t)mlen);
    if (!mbuf) {
        return NULL;
    }

    if (WideCharToMultiByte(cp, 0, wstr, -1, mbuf, mlen, NULL, NULL) <= 0) {
        xx_mem_free(mbuf);
        return NULL;
    }

    return mbuf;
}


/* Runtime string primitives (hand-rolled so the /NODEFAULTLIB build needs no CRT string functions).
 * These define the public xx_rt_str* names directly, as
 * xx_rt_utf8_to_utf16 in this file already does - no wrapper layer. */
/* --- Runtime string primitives: Win32 implementations --- */

size_t xx_rt_strlen(const char *pString)
{
    return (size_t)lstrlenA(pString);
}

/* lstrcmpA is deliberately NOT used: it compares with the user's locale via
 * CompareStringA, so "a" vs "B" and punctuation would order differently from
 * a byte comparison. The signature database is sorted with this function and
 * JavaScript relational operators rely on it, so the ordering has to be
 * plain unsigned byte order on every platform.                              */
int xx_rt_strcmp(const char *pLeft, const char *pRight)
{
    const unsigned char *pA = (const unsigned char *)pLeft;
    const unsigned char *pB = (const unsigned char *)pRight;

    while (*pA && (*pA == *pB)) {
        pA++;
        pB++;
    }

    return (int)*pA - (int)*pB;
}

int xx_rt_strncmp(const char *pLeft, const char *pRight, size_t nSize)
{
    const unsigned char *pA = (const unsigned char *)pLeft;
    const unsigned char *pB = (const unsigned char *)pRight;
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        if (pA[i] != pB[i]) {
            return (int)pA[i] - (int)pB[i];
        }

        if (pA[i] == 0) {
            break;
        }
    }

    return 0;
}

/* strncpy semantics: copy at most nSize bytes, zero-pad the remainder, and do
 * not terminate when the source is longer. lstrcpynA differs on both counts,
 * so it is not used here.                                                   */
char *xx_rt_strncpy(char *pDestination, const char *pSource, size_t nSize)
{
    size_t i = 0;

    for (i = 0; (i < nSize) && pSource[i]; i++) {
        pDestination[i] = pSource[i];
    }

    for (; i < nSize; i++) {
        pDestination[i] = 0;
    }

    return pDestination;
}

char *xx_rt_strchr(const char *pString, int nChar)
{
    char nWanted = (char)nChar;

    for (;;) {
        if (*pString == nWanted) {
            return (char *)pString;
        }

        if (*pString == 0) {
            return NULL;
        }

        pString++;
    }
}

char *xx_rt_strrchr(const char *pString, int nChar)
{
    char nWanted = (char)nChar;
    const char *pFound = NULL;

    for (;;) {
        if (*pString == nWanted) {
            pFound = pString;
        }

        if (*pString == 0) {
            break;
        }

        pString++;
    }

    return (char *)pFound;
}

char *xx_rt_strstr(const char *pHaystack, const char *pNeedle)
{
    size_t nNeedle = xx_rt_strlen(pNeedle);
    size_t i = 0;

    if (nNeedle == 0) {
        return (char *)pHaystack;
    }

    for (i = 0; pHaystack[i]; i++) {
        size_t j = 0;

        while ((j < nNeedle) && (pHaystack[i + j] == pNeedle[j])) {
            j++;
        }

        if (j == nNeedle) {
            return (char *)(pHaystack + i);
        }
    }

    return NULL;
}

#endif /* _WIN32 */
