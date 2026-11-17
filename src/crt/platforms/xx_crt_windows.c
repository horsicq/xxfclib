/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_windows.c
 * @brief The standard-name C runtime for Windows programs linked without
 *        any Microsoft C runtime: <string.h>, <stdlib.h>, <ctype.h>,
 *        <math.h>, <time.h>, <locale.h>, errno and the file-system calls
 *        that xxyara and its tests use.
 *
 * This unit, xx_crt_stdio_windows.c, xx_crt_entry_windows.c and
 * xx_crt_tls_windows.c together replace libucrt, libvcruntime and libcmt
 * for a /NODEFAULTLIB link that imports nothing but KERNEL32. They define
 * the standard names themselves, so they must never be linked into a
 * program that also links the Microsoft C runtime (duplicate symbols); the
 * portable cores they call (include/xxfclib/crt/xx_crt.h) can be.
 *
 * Every function keeps the observable behaviour of the Universal CRT that
 * MSVC programs, YARA-X's official binaries among them, are built with:
 * see xx_crt.h for the conversion and formatting rules and the comments
 * below for the rest (malloc(0) is a one-byte block, realloc(p, 0) frees,
 * _getcwd reports ERANGE, narrow file names use the ANSI code page, ...).
 * The differential test tests/crt/crt_ucrt_diff.c checks the cores against
 * UCRT; tests/crt/crt_platform_probe.c, built against both runtimes, checks
 * what exists only under standard names (environment, paths, stat, errno of
 * the math functions).
 */

#if defined(_WIN32) && defined(_MSC_VER)

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ctype.h>
#include <direct.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>

#include "xxfclib/crt/xx_crt.h"
#include "xx_crt_windows_internal.h"

#if defined(_MSC_VER)
/* These are compiler intrinsics; defining them needs their function form. */
#pragma function(memcpy, memmove, memset, memcmp, memchr, strlen, strcmp, strcpy, strcat)
#pragma function(wcslen, wcscmp, wcscpy, wcscat)
#pragma function(fabs, floor, ceil, sqrt, abs, labs, llabs, log2)
#pragma function(_dclass, _ldclass, _fdclass, _dtest, _ldtest, _fdtest)
#endif

/* ------------------------------------------------------------------ errno -- */

static __declspec(thread) int t_xx_crt_errno;
static __declspec(thread) unsigned long t_xx_crt_doserrno;

int *__cdecl _errno(void)
{
    return &t_xx_crt_errno;
}

unsigned long *__cdecl __doserrno(void)
{
    return &t_xx_crt_doserrno;
}

errno_t __cdecl _get_errno(int *pValue)
{
    if (!pValue) return EINVAL;
    *pValue = t_xx_crt_errno;
    return 0;
}

errno_t __cdecl _set_errno(int nValue)
{
    t_xx_crt_errno = nValue;
    return 0;
}

/* ----------------------------------------------------------------- memory -- */

/* _HEAP_MAXREQ of UCRT */
#define XX_CRT_HEAP_MAXREQ ((size_t)0xFFFFFFFFFFFFFFE0ull)

static HANDLE xx_crt_heap(void)
{
    static HANDLE s_hHeap;
    HANDLE hHeap = s_hHeap;
    if (!hHeap) {
        hHeap = GetProcessHeap();
        s_hHeap = hHeap;
    }
    return hHeap;
}

void *__cdecl malloc(size_t nSize)
{
    void *p;
    if (nSize > XX_CRT_HEAP_MAXREQ) {
        errno = ENOMEM;
        return NULL;
    }
    p = HeapAlloc(xx_crt_heap(), 0, nSize ? nSize : 1);
    if (!p) errno = ENOMEM;
    return p;
}

void *__cdecl calloc(size_t nCount, size_t nSize)
{
    void *p;
    size_t nTotal;
    if (nCount && XX_CRT_HEAP_MAXREQ / nCount < nSize) {
        errno = ENOMEM;
        return NULL;
    }
    nTotal = nCount * nSize;
    p = HeapAlloc(xx_crt_heap(), HEAP_ZERO_MEMORY, nTotal ? nTotal : 1);
    if (!p) errno = ENOMEM;
    return p;
}

void *__cdecl realloc(void *pBlock, size_t nSize)
{
    void *p;
    if (!pBlock) return malloc(nSize);
    if (nSize == 0) {
        HeapFree(xx_crt_heap(), 0, pBlock);
        return NULL;
    }
    if (nSize > XX_CRT_HEAP_MAXREQ) {
        errno = ENOMEM;
        return NULL;
    }
    p = HeapReAlloc(xx_crt_heap(), 0, pBlock, nSize);
    if (!p) errno = ENOMEM;
    return p;
}

void __cdecl free(void *pBlock)
{
    if (pBlock) HeapFree(xx_crt_heap(), 0, pBlock);
}

size_t __cdecl _msize(void *pBlock)
{
    if (!pBlock) {
        errno = EINVAL;
        return (size_t)-1;
    }
    return HeapSize(xx_crt_heap(), 0, pBlock);
}

/* ---------------------------------------------------------------- strings -- */

void *__cdecl memcpy(void *pDestination, const void *pSource, size_t nSize)
{
    return xx_crt_memcpy(pDestination, pSource, nSize);
}

void *__cdecl memmove(void *pDestination, const void *pSource, size_t nSize)
{
    return xx_crt_memmove(pDestination, pSource, nSize);
}

void *__cdecl memset(void *pDestination, int nValue, size_t nSize)
{
    return xx_crt_memset(pDestination, nValue, nSize);
}

int __cdecl memcmp(const void *pLeft, const void *pRight, size_t nSize)
{
    return xx_crt_memcmp(pLeft, pRight, nSize);
}

void *__cdecl memchr(const void *pMemory, int nChar, size_t nSize)
{
    return xx_crt_memchr(pMemory, nChar, nSize);
}

size_t __cdecl strlen(const char *pString)
{
    return xx_crt_strlen(pString);
}

size_t __cdecl strnlen(const char *pString, size_t nMax)
{
    const char *pEnd = (const char *)xx_crt_memchr(pString, 0, nMax);
    return pEnd ? (size_t)(pEnd - pString) : nMax;
}

/* strcmp and strncmp answer -1, 0 or 1, as UCRT's do. */
int __cdecl strcmp(const char *pLeft, const char *pRight)
{
    const unsigned char *a = (const unsigned char *)pLeft;
    const unsigned char *b = (const unsigned char *)pRight;
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b ? 0 : (*a < *b ? -1 : 1);
}

int __cdecl strncmp(const char *pLeft, const char *pRight, size_t nCount)
{
    const unsigned char *a = (const unsigned char *)pLeft;
    const unsigned char *b = (const unsigned char *)pRight;
    if (nCount == 0) return 0;
    while (--nCount && *a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b ? 0 : (*a < *b ? -1 : 1);
}

int __cdecl _strnicmp(const char *pLeft, const char *pRight, size_t nCount)
{
    return xx_crt_strnicmp(pLeft, pRight, nCount);
}

int __cdecl _stricmp(const char *pLeft, const char *pRight)
{
    return xx_crt_stricmp(pLeft, pRight);
}

char *__cdecl strcpy(char *pDestination, const char *pSource)
{
    size_t nLength = xx_crt_strlen(pSource);
    xx_crt_memcpy(pDestination, pSource, nLength + 1);
    return pDestination;
}

char *__cdecl strncpy(char *pDestination, const char *pSource, size_t nCount)
{
    size_t nLength = strnlen(pSource, nCount);
    xx_crt_memcpy(pDestination, pSource, nLength);
    if (nLength < nCount) xx_crt_memset(pDestination + nLength, 0, nCount - nLength);
    return pDestination;
}

char *__cdecl strcat(char *pDestination, const char *pSource)
{
    strcpy(pDestination + xx_crt_strlen(pDestination), pSource);
    return pDestination;
}

char *__cdecl strncat(char *pDestination, const char *pSource, size_t nCount)
{
    char *pEnd = pDestination + xx_crt_strlen(pDestination);
    size_t nLength = strnlen(pSource, nCount);
    xx_crt_memcpy(pEnd, pSource, nLength);
    pEnd[nLength] = 0;
    return pDestination;
}

char *__cdecl strchr(const char *pString, int nChar)
{
    return xx_crt_strchr(pString, nChar);
}

char *__cdecl strrchr(const char *pString, int nChar)
{
    char c = (char)nChar;
    const char *pLast = NULL;
    for (;; ++pString) {
        if (*pString == c) pLast = pString;
        if (!*pString) return (char *)pLast;
    }
}

/* The two-way string matching of Crochemore and Perrin (1991), in the
 * constant-space form without a shift table that musl libc also uses: the
 * needle is split at a critical position, the right part is compared
 * forwards and the left part backwards, and a mismatch shifts by the period.
 * At most a small constant times (haystack + needle) byte comparisons. */
static const char *xx_crt_strstr_two_way(const unsigned char *pHaystack, size_t nHaystack, const unsigned char *pNeedle, size_t nNeedle)
{
    const unsigned char *pEnd = pHaystack + nHaystack;
    size_t nSuffix, nPeriod, nSuffix2, nPeriod2;
    size_t i, j, k, nMemory, nMemoryStart;

    /* the maximal suffix for <, then for > */
    i = (size_t)-1;
    j = 0;
    k = nPeriod = 1;
    while (j + k < nNeedle) {
        unsigned char a = pNeedle[i + k], b = pNeedle[j + k];
        if (a == b) {
            if (k == nPeriod) {
                j += nPeriod;
                k = 1;
            } else {
                ++k;
            }
        } else if (a > b) {
            j += k;
            k = 1;
            nPeriod = j - i;
        } else {
            i = j++;
            k = nPeriod = 1;
        }
    }
    nSuffix = i;
    i = (size_t)-1;
    j = 0;
    k = nPeriod2 = 1;
    while (j + k < nNeedle) {
        unsigned char a = pNeedle[i + k], b = pNeedle[j + k];
        if (a == b) {
            if (k == nPeriod2) {
                j += nPeriod2;
                k = 1;
            } else {
                ++k;
            }
        } else if (a < b) {
            j += k;
            k = 1;
            nPeriod2 = j - i;
        } else {
            i = j++;
            k = nPeriod2 = 1;
        }
    }
    nSuffix2 = i;
    if (nSuffix2 + 1 > nSuffix + 1) {
        nSuffix = nSuffix2;
        nPeriod = nPeriod2;
    }
    /* a periodic needle keeps a memory of the matched prefix */
    if (xx_crt_memcmp(pNeedle, pNeedle + nPeriod, nSuffix + 1) != 0) {
        nMemoryStart = 0;
        nPeriod = (nSuffix > nNeedle - nSuffix - 1 ? nSuffix : nNeedle - nSuffix - 1) + 1;
    } else {
        nMemoryStart = nNeedle - nPeriod;
    }
    nMemory = 0;
    for (;;) {
        if ((size_t)(pEnd - pHaystack) < nNeedle) return NULL;
        /* the right part */
        for (k = nSuffix + 1 > nMemory ? nSuffix + 1 : nMemory; k < nNeedle && pNeedle[k] == pHaystack[k]; ++k) {
        }
        if (k < nNeedle) {
            pHaystack += k - nSuffix;
            nMemory = 0;
            continue;
        }
        /* the left part */
        for (k = nSuffix + 1; k > nMemory && pNeedle[k - 1] == pHaystack[k - 1]; --k) {
        }
        if (k <= nMemory) return (const char *)pHaystack;
        pHaystack += nPeriod;
        nMemory = nMemoryStart;
    }
}

char *__cdecl strstr(const char *pHaystack, const char *pNeedle)
{
    size_t nNeedle = xx_crt_strlen(pNeedle);
    size_t nHaystack;
    const char *p = pHaystack;
    if (nNeedle == 0) return (char *)pHaystack;
    nHaystack = xx_crt_strlen(pHaystack);
    if (nHaystack < nNeedle) return NULL;
    /* A short needle: find its first byte, compare the rest. The work is at
     * most 32 comparisons per position; longer needles use the linear-time
     * two-way search so that no input makes strstr quadratic. */
    if (nNeedle > 32) return (char *)xx_crt_strstr_two_way((const unsigned char *)pHaystack, nHaystack, (const unsigned char *)pNeedle, nNeedle);
    while (nHaystack >= nNeedle) {
        const char *pHit = (const char *)xx_crt_memchr(p, (unsigned char)pNeedle[0], nHaystack - nNeedle + 1);
        if (!pHit) return NULL;
        nHaystack -= (size_t)(pHit - p);
        p = pHit;
        if (xx_crt_memcmp(p, pNeedle, nNeedle) == 0) return (char *)p;
        ++p;
        --nHaystack;
    }
    return NULL;
}

size_t __cdecl strspn(const char *pString, const char *pAccept)
{
    unsigned char aSet[256];
    size_t i;
    xx_crt_memset(aSet, 0, sizeof(aSet));
    while (*pAccept) aSet[(unsigned char)*pAccept++] = 1;
    for (i = 0; pString[i] && aSet[(unsigned char)pString[i]]; ++i) {
    }
    return i;
}

size_t __cdecl strcspn(const char *pString, const char *pReject)
{
    unsigned char aSet[256];
    size_t i;
    xx_crt_memset(aSet, 0, sizeof(aSet));
    while (*pReject) aSet[(unsigned char)*pReject++] = 1;
    for (i = 0; pString[i] && !aSet[(unsigned char)pString[i]]; ++i) {
    }
    return i;
}

char *__cdecl strpbrk(const char *pString, const char *pAccept)
{
    size_t i = strcspn(pString, pAccept);
    return pString[i] ? (char *)(pString + i) : NULL;
}

char *__cdecl _strdup(const char *pString)
{
    size_t nLength;
    char *pCopy;
    if (!pString) return NULL;
    nLength = xx_crt_strlen(pString) + 1;
    pCopy = (char *)malloc(nLength);
    if (pCopy) xx_crt_memcpy(pCopy, pString, nLength);
    return pCopy;
}

size_t __cdecl wcslen(const wchar_t *pString)
{
    const wchar_t *p = pString;
    while (*p) ++p;
    return (size_t)(p - pString);
}

int __cdecl wcscmp(const wchar_t *pLeft, const wchar_t *pRight)
{
    while (*pLeft && *pLeft == *pRight) {
        ++pLeft;
        ++pRight;
    }
    return *pLeft == *pRight ? 0 : (*pLeft < *pRight ? -1 : 1);
}

int __cdecl wcsncmp(const wchar_t *pLeft, const wchar_t *pRight, size_t nCount)
{
    if (nCount == 0) return 0;
    while (--nCount && *pLeft && *pLeft == *pRight) {
        ++pLeft;
        ++pRight;
    }
    return *pLeft == *pRight ? 0 : (*pLeft < *pRight ? -1 : 1);
}

wchar_t *__cdecl wcschr(const wchar_t *pString, wchar_t nChar)
{
    for (;; ++pString) {
        if (*pString == nChar) return (wchar_t *)pString;
        if (!*pString) return NULL;
    }
}

wchar_t *__cdecl wcsrchr(const wchar_t *pString, wchar_t nChar)
{
    const wchar_t *pLast = NULL;
    for (;; ++pString) {
        if (*pString == nChar) pLast = pString;
        if (!*pString) return (wchar_t *)pLast;
    }
}

wchar_t *__cdecl wcscpy(wchar_t *pDestination, const wchar_t *pSource)
{
    xx_crt_memcpy(pDestination, pSource, (wcslen(pSource) + 1) * sizeof(wchar_t));
    return pDestination;
}

wchar_t *__cdecl wcscat(wchar_t *pDestination, const wchar_t *pSource)
{
    wcscpy(pDestination + wcslen(pDestination), pSource);
    return pDestination;
}

/* ------------------------------------------------------------------ ctype -- */

int __cdecl isalpha(int c) { return xx_crt_ctype(c) & 0x103; }
int __cdecl isupper(int c) { return xx_crt_ctype(c) & 0x1; }
int __cdecl islower(int c) { return xx_crt_ctype(c) & 0x2; }
int __cdecl isdigit(int c) { return xx_crt_ctype(c) & 0x4; }
int __cdecl isxdigit(int c) { return xx_crt_ctype(c) & 0x80; }
int __cdecl isspace(int c) { return xx_crt_ctype(c) & 0x8; }
int __cdecl ispunct(int c) { return xx_crt_ctype(c) & 0x10; }
int __cdecl isalnum(int c) { return xx_crt_ctype(c) & 0x107; }
int __cdecl isprint(int c) { return xx_crt_ctype(c) & 0x157; }
int __cdecl isgraph(int c) { return xx_crt_ctype(c) & 0x117; }
int __cdecl iscntrl(int c) { return xx_crt_ctype(c) & 0x20; }
int __cdecl isblank(int c) { return c == '\t' ? 0x40 : (xx_crt_ctype(c) & 0x40); }
int __cdecl tolower(int c) { return xx_crt_tolower(c); }
int __cdecl toupper(int c) { return xx_crt_toupper(c); }

/* ------------------------------------------------------------ conversions -- */

double __cdecl strtod(const char *pString, char **ppEnd)
{
    int nError = 0;
    double nValue = xx_crt_strtod(pString, ppEnd, &nError);
    if (nError) errno = nError;
    return nValue;
}

float __cdecl strtof(const char *pString, char **ppEnd)
{
    int nError = 0;
    float nValue = xx_crt_strtof(pString, ppEnd, &nError);
    if (nError) errno = nError;
    return nValue;
}

double __cdecl atof(const char *pString)
{
    return strtod(pString, NULL);
}

long __cdecl strtol(const char *pString, char **ppEnd, int nBase)
{
    int nError = 0;
    long nValue = xx_crt_strtol(pString, ppEnd, nBase, &nError);
    if (nError) errno = nError;
    return nValue;
}

unsigned long __cdecl strtoul(const char *pString, char **ppEnd, int nBase)
{
    int nError = 0;
    unsigned long nValue = xx_crt_strtoul(pString, ppEnd, nBase, &nError);
    if (nError) errno = nError;
    return nValue;
}

long long __cdecl strtoll(const char *pString, char **ppEnd, int nBase)
{
    int nError = 0;
    long long nValue = xx_crt_strtoll(pString, ppEnd, nBase, &nError);
    if (nError) errno = nError;
    return nValue;
}

unsigned long long __cdecl strtoull(const char *pString, char **ppEnd, int nBase)
{
    int nError = 0;
    unsigned long long nValue = xx_crt_strtoull(pString, ppEnd, nBase, &nError);
    if (nError) errno = nError;
    return nValue;
}

long long __cdecl _strtoi64(const char *pString, char **ppEnd, int nBase)
{
    return strtoll(pString, ppEnd, nBase);
}

unsigned long long __cdecl _strtoui64(const char *pString, char **ppEnd, int nBase)
{
    return strtoull(pString, ppEnd, nBase);
}

int __cdecl atoi(const char *pString)
{
    return (int)strtol(pString, NULL, 10);
}

long __cdecl atol(const char *pString)
{
    return strtol(pString, NULL, 10);
}

long long __cdecl atoll(const char *pString)
{
    return strtoll(pString, NULL, 10);
}

int __cdecl abs(int nValue)
{
    return nValue < 0 ? -nValue : nValue;
}

long __cdecl labs(long nValue)
{
    return nValue < 0 ? -nValue : nValue;
}

long long __cdecl llabs(long long nValue)
{
    return nValue < 0 ? -nValue : nValue;
}

void __cdecl qsort(void *pBase, size_t nCount, size_t nSize, _CoreCrtNonSecureSearchSortCompareFunction fnCompare)
{
    xx_crt_qsort(pBase, nCount, nSize, fnCompare);
}

void __cdecl qsort_s(void *pBase, rsize_t nCount, rsize_t nSize, _CoreCrtSecureSearchSortCompareFunction fnCompare, void *pContext)
{
    xx_crt_qsort_s(pBase, nCount, nSize, fnCompare, pContext);
}

void *__cdecl bsearch(const void *pKey, const void *pBase, size_t nCount, size_t nSize, _CoreCrtNonSecureSearchSortCompareFunction fnCompare)
{
    const char *pLow = (const char *)pBase;
    while (nCount) {
        size_t nHalf = nCount / 2;
        const char *pMiddle = pLow + nHalf * nSize;
        int nOrder = fnCompare(pKey, pMiddle);
        if (nOrder == 0) return (void *)pMiddle;
        if (nOrder > 0) {
            pLow = pMiddle + nSize;
            nCount -= nHalf + 1;
        } else {
            nCount = nHalf;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------- math -- */

double __cdecl floor(double nValue) { return xx_crt_floor(nValue); }
double __cdecl ceil(double nValue) { return xx_crt_ceil(nValue); }
double __cdecl trunc(double nValue) { return xx_crt_trunc(nValue); }
double __cdecl fabs(double nValue) { return xx_crt_fabs(nValue); }
/* sqrt and log2 report errors like UCRT: EDOM for a negative argument,
 * ERANGE for log2(+-0); nothing for a NaN argument. */
double __cdecl sqrt(double nValue)
{
    int nError = xx_crt_sqrt_errno(nValue);
    if (nError) errno = nError;
    return xx_crt_sqrt(nValue);
}

/* log2 is xx_ms_log2: UCRT's exact results (UCRT's log2 is not correctly
 * rounded, and YARA-X's math.entropy carries its last bits). */
double __cdecl log2(double nValue)
{
    int nError = xx_crt_log2_errno(nValue);
    if (nError) errno = nError;
    return xx_ms_log2(nValue);
}
short __cdecl _dclass(double nValue) { return xx_crt_dclass(nValue); }
int __cdecl _dsign(double nValue) { return xx_crt_dsign(nValue); }
short __cdecl _ldclass(long double nValue) { return xx_crt_dclass((double)nValue); }
int __cdecl _ldsign(long double nValue) { return xx_crt_dsign((double)nValue); }
short __cdecl _fdclass(float nValue) { return xx_crt_fdclass(nValue); }
int __cdecl _fdsign(float nValue) { return xx_crt_fdsign(nValue); }
short __cdecl _dtest(double *pValue) { return xx_crt_dclass(*pValue); }
short __cdecl _ldtest(long double *pValue) { return xx_crt_dclass((double)*pValue); }
short __cdecl _fdtest(float *pValue) { return xx_crt_fdclass(*pValue); }

/* ----------------------------------------------------------------- locale -- */

struct lconv *__cdecl localeconv(void)
{
    static char s_point[] = ".";
    static char s_empty[] = "";
    static wchar_t s_wpoint[] = L".";
    static wchar_t s_wempty[] = L"";
    static struct lconv s_lconv;
    if (!s_lconv.decimal_point) {
        s_lconv.thousands_sep = s_empty;
        s_lconv.grouping = s_empty;
        s_lconv.int_curr_symbol = s_empty;
        s_lconv.currency_symbol = s_empty;
        s_lconv.mon_decimal_point = s_empty;
        s_lconv.mon_thousands_sep = s_empty;
        s_lconv.mon_grouping = s_empty;
        s_lconv.positive_sign = s_empty;
        s_lconv.negative_sign = s_empty;
        s_lconv.int_frac_digits = CHAR_MAX;
        s_lconv.frac_digits = CHAR_MAX;
        s_lconv.p_cs_precedes = CHAR_MAX;
        s_lconv.p_sep_by_space = CHAR_MAX;
        s_lconv.n_cs_precedes = CHAR_MAX;
        s_lconv.n_sep_by_space = CHAR_MAX;
        s_lconv.p_sign_posn = CHAR_MAX;
        s_lconv.n_sign_posn = CHAR_MAX;
        s_lconv._W_decimal_point = s_wpoint;
        s_lconv._W_thousands_sep = s_wempty;
        s_lconv._W_int_curr_symbol = s_wempty;
        s_lconv._W_currency_symbol = s_wempty;
        s_lconv._W_mon_decimal_point = s_wempty;
        s_lconv._W_mon_thousands_sep = s_wempty;
        s_lconv._W_positive_sign = s_wempty;
        s_lconv._W_negative_sign = s_wempty;
        s_lconv.decimal_point = s_point;
    }
    return &s_lconv;
}

char *__cdecl strerror(int nError)
{
    return (char *)xx_crt_strerror_text(nError);
}

/* ------------------------------------------------------------------- time -- */

__time64_t __cdecl _time64(__time64_t *pTime)
{
    FILETIME fileTime;
    ULARGE_INTEGER value;
    __time64_t nTime;
    GetSystemTimeAsFileTime(&fileTime);
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    nTime = (__time64_t)((value.QuadPart - 116444736000000000ull) / 10000000ull);
    if (pTime) *pTime = nTime;
    return nTime;
}

static __declspec(thread) struct tm t_xx_crt_tm;

errno_t __cdecl _gmtime64_s(struct tm *pResult, const __time64_t *pTime)
{
    int aFields[9];
    int nError;
    if (!pResult || !pTime) return errno = EINVAL;
    nError = xx_crt_gmtime(*pTime, aFields);
    if (nError) {
        xx_crt_memset(pResult, 0xFF, sizeof(*pResult));
        return errno = nError;
    }
    pResult->tm_sec = aFields[0];
    pResult->tm_min = aFields[1];
    pResult->tm_hour = aFields[2];
    pResult->tm_mday = aFields[3];
    pResult->tm_mon = aFields[4];
    pResult->tm_year = aFields[5];
    pResult->tm_wday = aFields[6];
    pResult->tm_yday = aFields[7];
    pResult->tm_isdst = aFields[8];
    return 0;
}

struct tm *__cdecl _gmtime64(const __time64_t *pTime)
{
    return _gmtime64_s(&t_xx_crt_tm, pTime) == 0 ? &t_xx_crt_tm : NULL;
}

size_t __cdecl strftime(char *pBuffer, size_t nSize, const char *pFormat, const struct tm *pTime)
{
    int aFields[9];
    if (!pFormat || !pTime) {
        errno = EINVAL;
        return 0;
    }
    aFields[0] = pTime->tm_sec;
    aFields[1] = pTime->tm_min;
    aFields[2] = pTime->tm_hour;
    aFields[3] = pTime->tm_mday;
    aFields[4] = pTime->tm_mon;
    aFields[5] = pTime->tm_year;
    aFields[6] = pTime->tm_wday;
    aFields[7] = pTime->tm_yday;
    aFields[8] = pTime->tm_isdst;
    {
        /* %z and %Z: the system time zone, as UCRT's _tzset reads it when no
         * TZ variable is set (standard or daylight by tm_isdst) */
        TIME_ZONE_INFORMATION zone;
        char sZone[64];
        long nBias = 0;
        const char *p;
        int bZone = 0;
        sZone[0] = 0;
        for (p = pFormat; *p; ++p) {
            if (p[0] == '%' && p[1]) {
                ++p;
                if (*p == '#' && p[1]) ++p;
                if (*p == 'z' || *p == 'Z') bZone = 1;
            }
        }
        if (!bZone) return xx_crt_strftime(pBuffer, nSize, pFormat, aFields);
        if (GetTimeZoneInformation(&zone) != TIME_ZONE_ID_INVALID) {
            int bDaylight = pTime->tm_isdst > 0;
            int nLength;
            nBias = zone.Bias + (bDaylight ? zone.DaylightBias : (zone.StandardDate.wMonth ? zone.StandardBias : 0));
            nLength = WideCharToMultiByte(CP_ACP, 0, bDaylight ? zone.DaylightName : zone.StandardName, -1, sZone, (int)sizeof(sZone) - 1, NULL, NULL);
            if (nLength <= 0) sZone[0] = 0;
            sZone[sizeof(sZone) - 1] = 0;
        }
        return xx_crt_strftime_zone(pBuffer, nSize, pFormat, aFields, sZone, -nBias * 60);
    }
}

static LARGE_INTEGER s_xx_crt_clock_start;

void xx_crt_clock_start(void)
{
    if (s_xx_crt_clock_start.QuadPart == 0) QueryPerformanceCounter(&s_xx_crt_clock_start);
}

clock_t __cdecl clock(void)
{
    LARGE_INTEGER now, frequency;
    long long nTicks;
    xx_crt_clock_start();
    if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return -1;
    nTicks = now.QuadPart - s_xx_crt_clock_start.QuadPart;
    return (clock_t)((nTicks / frequency.QuadPart) * CLOCKS_PER_SEC + ((nTicks % frequency.QuadPart) * CLOCKS_PER_SEC) / frequency.QuadPart);
}

/* ------------------------------------------------- names and environment -- */

static UINT xx_crt_file_code_page(void)
{
    return AreFileApisANSI() ? CP_ACP : CP_OEMCP;
}

wchar_t *xx_crt_narrow_to_wide(const char *pText)
{
    int nLength;
    wchar_t *pWide;
    if (!pText) return NULL;
    nLength = MultiByteToWideChar(xx_crt_file_code_page(), 0, pText, -1, NULL, 0);
    if (nLength <= 0) return NULL;
    pWide = (wchar_t *)malloc((size_t)nLength * sizeof(wchar_t));
    if (pWide && MultiByteToWideChar(xx_crt_file_code_page(), 0, pText, -1, pWide, nLength) <= 0) {
        free(pWide);
        return NULL;
    }
    return pWide;
}

char *xx_crt_wide_to_narrow(const wchar_t *pText)
{
    int nLength;
    char *pNarrow;
    if (!pText) return NULL;
    nLength = WideCharToMultiByte(xx_crt_file_code_page(), 0, pText, -1, NULL, 0, NULL, NULL);
    if (nLength <= 0) return NULL;
    pNarrow = (char *)malloc((size_t)nLength);
    if (pNarrow && WideCharToMultiByte(xx_crt_file_code_page(), 0, pText, -1, pNarrow, nLength, NULL, NULL) <= 0) {
        free(pNarrow);
        return NULL;
    }
    return pNarrow;
}

/* The environment as UCRT keeps it: a table copied from the process
 * environment on first use and changed only by _putenv_s. getenv and
 * _wgetenv look a name up without regard to case and return a pointer into
 * the table. Like UCRT, they do not see a later SetEnvironmentVariable by
 * other code. Unlike UCRT, a returned pointer stays valid after the variable
 * is replaced or removed: every block lives until the runtime shuts down.
 * One entry per variable, so the work of a lookup does not grow with the
 * number of changes. */
typedef struct xx_crt_env_block_s {
    struct xx_crt_env_block_s *pNext;
} xx_crt_env_block;

typedef struct xx_crt_env_entry_s {
    struct xx_crt_env_entry_s *pNext;
    wchar_t *pName;
    int nNameLength;
    wchar_t *pValue;
    char *pNarrowValue; /* made on the first getenv of the current value */
} xx_crt_env_entry;

static xx_crt_env_entry *s_xx_crt_env;
static xx_crt_env_block *s_xx_crt_env_blocks;
static int s_xx_crt_env_ready;
static SRWLOCK s_xx_crt_env_lock = SRWLOCK_INIT;

static void *xx_crt_env_alloc(size_t nSize)
{
    xx_crt_env_block *pBlock = (xx_crt_env_block *)malloc(sizeof(xx_crt_env_block) + nSize);
    if (!pBlock) return NULL;
    pBlock->pNext = s_xx_crt_env_blocks;
    s_xx_crt_env_blocks = pBlock;
    return pBlock + 1;
}

static wchar_t *xx_crt_env_copy(const wchar_t *pText, size_t nLength)
{
    wchar_t *pCopy = (wchar_t *)xx_crt_env_alloc((nLength + 1) * sizeof(wchar_t));
    if (!pCopy) return NULL;
    xx_crt_memcpy(pCopy, pText, nLength * sizeof(wchar_t));
    pCopy[nLength] = 0;
    return pCopy;
}

static xx_crt_env_entry **xx_crt_env_find(const wchar_t *pName, size_t nLength)
{
    xx_crt_env_entry **ppEntry;
    for (ppEntry = &s_xx_crt_env; *ppEntry; ppEntry = &(*ppEntry)->pNext) {
        if ((size_t)(*ppEntry)->nNameLength == nLength &&
            CompareStringOrdinal((*ppEntry)->pName, (int)nLength, pName, (int)nLength, TRUE) == CSTR_EQUAL) {
            return ppEntry;
        }
    }
    return NULL;
}

/* Sets or (pValue == NULL) removes a variable in the table; 0 or ENOMEM. */
static int xx_crt_env_set(const wchar_t *pName, size_t nLength, const wchar_t *pValue)
{
    xx_crt_env_entry **ppEntry = xx_crt_env_find(pName, nLength);
    wchar_t *pCopy;
    if (!pValue) {
        if (ppEntry) *ppEntry = (*ppEntry)->pNext;
        return 0;
    }
    pCopy = xx_crt_env_copy(pValue, wcslen(pValue));
    if (!pCopy) return ENOMEM;
    if (ppEntry) {
        (*ppEntry)->pValue = pCopy;
        (*ppEntry)->pNarrowValue = NULL;
    } else {
        xx_crt_env_entry *pEntry = (xx_crt_env_entry *)xx_crt_env_alloc(sizeof(xx_crt_env_entry));
        if (!pEntry) return ENOMEM;
        pEntry->pName = xx_crt_env_copy(pName, nLength);
        if (!pEntry->pName) return ENOMEM;
        pEntry->nNameLength = (int)nLength;
        pEntry->pValue = pCopy;
        pEntry->pNarrowValue = NULL;
        pEntry->pNext = s_xx_crt_env;
        s_xx_crt_env = pEntry;
    }
    return 0;
}

/* Called with the lock held. */
static void xx_crt_env_init(void)
{
    wchar_t *pStrings;
    const wchar_t *p;
    if (s_xx_crt_env_ready) return;
    s_xx_crt_env_ready = 1;
    pStrings = GetEnvironmentStringsW();
    if (!pStrings) return;
    for (p = pStrings; *p; p += wcslen(p) + 1) {
        /* "=C:=C:\dir" and the like are the shell's, not variables */
        const wchar_t *pEqual = p[0] == L'=' ? NULL : wcschr(p, L'=');
        if (pEqual && pEqual - p <= 0x7FFF) xx_crt_env_set(p, (size_t)(pEqual - p), pEqual + 1);
    }
    FreeEnvironmentStringsW(pStrings);
}

/* Frees the table (DLL unload). */
void xx_crt_env_shutdown(void)
{
    AcquireSRWLockExclusive(&s_xx_crt_env_lock);
    while (s_xx_crt_env_blocks) {
        xx_crt_env_block *pBlock = s_xx_crt_env_blocks;
        s_xx_crt_env_blocks = pBlock->pNext;
        free(pBlock);
    }
    s_xx_crt_env = NULL;
    s_xx_crt_env_ready = 0;
    ReleaseSRWLockExclusive(&s_xx_crt_env_lock);
}

wchar_t *__cdecl _wgetenv(const wchar_t *pName)
{
    xx_crt_env_entry **ppEntry;
    wchar_t *pResult;
    size_t nLength;
    if (!pName) {
        errno = EINVAL;
        return NULL;
    }
    nLength = wcslen(pName);
    if (nLength > 0x7FFF) return NULL;
    AcquireSRWLockExclusive(&s_xx_crt_env_lock);
    xx_crt_env_init();
    ppEntry = xx_crt_env_find(pName, nLength);
    pResult = ppEntry ? (*ppEntry)->pValue : NULL;
    ReleaseSRWLockExclusive(&s_xx_crt_env_lock);
    return pResult;
}

char *__cdecl getenv(const char *pName)
{
    wchar_t *pWideName;
    xx_crt_env_entry **ppEntry;
    char *pResult = NULL;
    size_t nLength;
    if (!pName) {
        errno = EINVAL;
        return NULL;
    }
    pWideName = xx_crt_narrow_to_wide(pName);
    if (!pWideName) return NULL;
    nLength = wcslen(pWideName);
    if (nLength <= 0x7FFF) {
        AcquireSRWLockExclusive(&s_xx_crt_env_lock);
        xx_crt_env_init();
        ppEntry = xx_crt_env_find(pWideName, nLength);
        if (ppEntry) {
            xx_crt_env_entry *pEntry = *ppEntry;
            if (!pEntry->pNarrowValue) {
                int nSize = WideCharToMultiByte(xx_crt_file_code_page(), 0, pEntry->pValue, -1, NULL, 0, NULL, NULL);
                char *pNarrow = nSize > 0 ? (char *)xx_crt_env_alloc((size_t)nSize) : NULL;
                if (pNarrow && WideCharToMultiByte(xx_crt_file_code_page(), 0, pEntry->pValue, -1, pNarrow, nSize, NULL, NULL) > 0) {
                    pEntry->pNarrowValue = pNarrow;
                }
            }
            pResult = pEntry->pNarrowValue;
        }
        ReleaseSRWLockExclusive(&s_xx_crt_env_lock);
    }
    free(pWideName);
    return pResult;
}

errno_t __cdecl _putenv_s(const char *pName, const char *pValue)
{
    wchar_t *pWideName;
    wchar_t *pWideValue;
    errno_t nResult = 0;
    if (!pName || !pValue || !*pName) return errno = EINVAL;
    pWideName = xx_crt_narrow_to_wide(pName);
    pWideValue = xx_crt_narrow_to_wide(pValue);
    if (!pWideName || !pWideValue) {
        free(pWideName);
        free(pWideValue);
        return errno = ENOMEM;
    }
    AcquireSRWLockExclusive(&s_xx_crt_env_lock);
    xx_crt_env_init();
    /* the process environment too, so that child processes inherit it; an
     * empty value removes the variable */
    if (!SetEnvironmentVariableW(pWideName, *pWideValue ? pWideValue : NULL) && *pWideValue) {
        nResult = EINVAL;
    } else if (wcslen(pWideName) <= 0x7FFF) {
        nResult = xx_crt_env_set(pWideName, wcslen(pWideName), *pWideValue ? pWideValue : NULL);
    }
    ReleaseSRWLockExclusive(&s_xx_crt_env_lock);
    free(pWideName);
    free(pWideValue);
    return nResult ? (errno = nResult) : 0;
}

/* ------------------------------------------------------------ file system -- */

static char *xx_crt_fill_path(char *pBuffer, size_t nBufferSize, const wchar_t *pWide)
{
    char *pNarrow = xx_crt_wide_to_narrow(pWide);
    size_t nLength;
    if (!pNarrow) {
        errno = EINVAL;
        return NULL;
    }
    nLength = xx_crt_strlen(pNarrow) + 1;
    if (!pBuffer) {
        if (nBufferSize < nLength) return pNarrow;
        pBuffer = (char *)malloc(nBufferSize);
        if (!pBuffer) {
            free(pNarrow);
            errno = ENOMEM;
            return NULL;
        }
        xx_crt_memcpy(pBuffer, pNarrow, nLength);
        free(pNarrow);
        return pBuffer;
    }
    if (nBufferSize < nLength) {
        free(pNarrow);
        errno = ERANGE;
        return NULL;
    }
    xx_crt_memcpy(pBuffer, pNarrow, nLength);
    free(pNarrow);
    return pBuffer;
}

/* The current directory in a malloc block. The size is asked first and the
 * call repeated while the answer does not fit: another thread may change
 * the directory to a longer one between the two calls. */
static wchar_t *xx_crt_current_directory(void)
{
    DWORD nSize = GetCurrentDirectoryW(0, NULL);
    for (;;) {
        wchar_t *pWide;
        DWORD nLength;
        if (nSize == 0) {
            errno = xx_crt_errno_from_oserror(GetLastError());
            return NULL;
        }
        pWide = (wchar_t *)malloc((size_t)nSize * sizeof(wchar_t));
        if (!pWide) {
            errno = ENOMEM;
            return NULL;
        }
        nLength = GetCurrentDirectoryW(nSize, pWide);
        if (nLength != 0 && nLength < nSize) return pWide; /* the length without the terminator */
        if (nLength == 0) errno = xx_crt_errno_from_oserror(GetLastError());
        free(pWide);
        if (nLength == 0) return NULL;
        nSize = nLength; /* too small: the size now needed */
    }
}

static wchar_t *xx_crt_full_path(const wchar_t *pPath)
{
    DWORD nSize = GetFullPathNameW(pPath, 0, NULL, NULL);
    for (;;) {
        wchar_t *pWide;
        DWORD nLength;
        if (nSize == 0) {
            errno = xx_crt_errno_from_oserror(GetLastError());
            return NULL;
        }
        pWide = (wchar_t *)malloc((size_t)nSize * sizeof(wchar_t));
        if (!pWide) {
            errno = ENOMEM;
            return NULL;
        }
        nLength = GetFullPathNameW(pPath, nSize, pWide, NULL);
        if (nLength != 0 && nLength < nSize) return pWide;
        if (nLength == 0) errno = xx_crt_errno_from_oserror(GetLastError());
        free(pWide);
        if (nLength == 0) return NULL;
        nSize = nLength;
    }
}

char *__cdecl _getcwd(char *pBuffer, int nSize)
{
    wchar_t *pWide;
    char *pResult;
    if (pBuffer && nSize <= 0) {
        errno = EINVAL;
        return NULL;
    }
    pWide = xx_crt_current_directory();
    if (!pWide) return NULL;
    pResult = xx_crt_fill_path(pBuffer, nSize > 0 ? (size_t)nSize : 0, pWide);
    free(pWide);
    return pResult;
}

char *__cdecl _fullpath(char *pBuffer, const char *pPath, size_t nSize)
{
    wchar_t *pWidePath;
    wchar_t *pWideFull;
    char *pResult;
    if (!pPath || !*pPath) return _getcwd(pBuffer, (int)(nSize > INT_MAX ? INT_MAX : nSize));
    pWidePath = xx_crt_narrow_to_wide(pPath);
    if (!pWidePath) {
        errno = EINVAL;
        return NULL;
    }
    pWideFull = xx_crt_full_path(pWidePath);
    free(pWidePath);
    if (!pWideFull) return NULL;
    pResult = xx_crt_fill_path(pBuffer, nSize, pWideFull);
    free(pWideFull);
    return pResult;
}

/* FILETIME (100 ns since 1601) to seconds since 1970; 0 stays 0. */
static __time64_t xx_crt_stat_time(FILETIME value)
{
    ULARGE_INTEGER n;
    n.LowPart = value.dwLowDateTime;
    n.HighPart = value.dwHighDateTime;
    if (n.QuadPart == 0) return 0;
    return (__time64_t)((long long)(n.QuadPart - 116444736000000000ull) / 10000000);
}

/* The drive number UCRT stores in st_dev and st_rdev: 0 for A:, from the
 * path when it names a drive, otherwise the current drive. */
static unsigned xx_crt_stat_drive(const wchar_t *pPath)
{
    wchar_t sCurrent[4];
    wchar_t nLetter = 0;
    if (pPath[0] && pPath[1] == L':') {
        nLetter = pPath[0];
    } else {
        wchar_t *pCurrent = xx_crt_current_directory();
        if (pCurrent) {
            sCurrent[0] = pCurrent[0];
            sCurrent[1] = pCurrent[1];
            free(pCurrent);
            if (sCurrent[1] == L':') nLetter = sCurrent[0];
        }
    }
    nLetter |= 0x20;
    return (nLetter >= L'a' && nLetter <= L'z') ? (unsigned)(nLetter - L'a') : 0;
}

/* The permission bits UCRT derives: read always, write unless read-only,
 * execute for directories and for .exe, .cmd, .bat and .com names; the
 * owner bits repeated for group and others. */
static unsigned short xx_crt_stat_mode(const wchar_t *pPath, DWORD nAttributes)
{
    unsigned short nMode = (nAttributes & FILE_ATTRIBUTE_DIRECTORY) ? (unsigned short)(_S_IFDIR | _S_IEXEC) : (unsigned short)_S_IFREG;
    nMode |= (nAttributes & FILE_ATTRIBUTE_READONLY) ? (unsigned short)_S_IREAD : (unsigned short)(_S_IREAD | _S_IWRITE);
    if (!(nAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        const wchar_t *pDot = wcsrchr(pPath, L'.');
        if (pDot && (CompareStringOrdinal(pDot, -1, L".exe", -1, TRUE) == CSTR_EQUAL || CompareStringOrdinal(pDot, -1, L".cmd", -1, TRUE) == CSTR_EQUAL ||
                     CompareStringOrdinal(pDot, -1, L".bat", -1, TRUE) == CSTR_EQUAL || CompareStringOrdinal(pDot, -1, L".com", -1, TRUE) == CSTR_EQUAL)) {
            nMode |= _S_IEXEC;
        }
    }
    nMode |= (unsigned short)((nMode & 0700) >> 3);
    nMode |= (unsigned short)((nMode & 0700) >> 6);
    return nMode;
}

/* Fills the structure from attributes, size and times; -1 with EOVERFLOW
 * when the size does not fit the 32-bit st_size, as UCRT. */
static int xx_crt_stat_fill(struct _stat64i32 *pStat, const wchar_t *pPath, DWORD nAttributes, DWORD nSizeHigh, DWORD nSizeLow, FILETIME created,
                            FILETIME accessed, FILETIME written)
{
    if (nSizeHigh != 0 || nSizeLow > 0x7FFFFFFFu) {
        errno = EOVERFLOW;
        return -1;
    }
    pStat->st_mode = xx_crt_stat_mode(pPath, nAttributes);
    pStat->st_nlink = 1;
    pStat->st_size = (_off_t)nSizeLow;
    pStat->st_dev = pStat->st_rdev = (_dev_t)xx_crt_stat_drive(pPath);
    pStat->st_mtime = xx_crt_stat_time(written);
    pStat->st_atime = accessed.dwLowDateTime || accessed.dwHighDateTime ? xx_crt_stat_time(accessed) : pStat->st_mtime;
    pStat->st_ctime = created.dwLowDateTime || created.dwHighDateTime ? xx_crt_stat_time(created) : pStat->st_mtime;
    return 0;
}

/* _wstat as UCRT answers it: wildcards and a bare drive ("C:") are ENOENT;
 * otherwise the path is opened for its attributes (directories included)
 * and described by file type, with FindFirstFile as the fallback for names
 * that cannot be opened. Mode, link count, size, drive number and the three
 * times are filled; st_ino, st_uid and st_gid are 0 as in UCRT. */
int __cdecl _wstat64i32(const wchar_t *pPath, struct _stat64i32 *pStat)
{
    HANDLE hHandle;
    if (!pPath || !pStat) {
        errno = EINVAL;
        return -1;
    }
    xx_crt_memset(pStat, 0, sizeof(*pStat));
    if (!*pPath || wcschr(pPath, L'*') || wcschr(pPath, L'?') ||
        (((pPath[0] | 0x20) >= L'a' && (pPath[0] | 0x20) <= L'z') && pPath[1] == L':' && pPath[2] == 0)) {
        errno = ENOENT;
        return -1;
    }
    hHandle = CreateFileW(pPath, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                          FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hHandle != INVALID_HANDLE_VALUE) {
        DWORD nType = GetFileType(hHandle);
        BY_HANDLE_FILE_INFORMATION information;
        int nResult;
        if (nType == FILE_TYPE_CHAR || nType == FILE_TYPE_PIPE) {
            /* a device: no times, no permissions, the "drive" is -1 */
            pStat->st_mode = nType == FILE_TYPE_CHAR ? _S_IFCHR : _S_IFIFO;
            pStat->st_nlink = 1;
            pStat->st_dev = pStat->st_rdev = (_dev_t)-1;
            CloseHandle(hHandle);
            return 0;
        }
        if (!GetFileInformationByHandle(hHandle, &information)) {
            errno = xx_crt_errno_from_oserror(GetLastError());
            CloseHandle(hHandle);
            return -1;
        }
        CloseHandle(hHandle);
        nResult = xx_crt_stat_fill(pStat, pPath, information.dwFileAttributes, information.nFileSizeHigh, information.nFileSizeLow, information.ftCreationTime,
                                   information.ftLastAccessTime, information.ftLastWriteTime);
        if (nResult != 0) xx_crt_memset(pStat, 0, sizeof(*pStat));
        return nResult;
    }
    {
        WIN32_FIND_DATAW data;
        HANDLE hFind = FindFirstFileExW(pPath, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, 0);
        int nResult;
        if (hFind == INVALID_HANDLE_VALUE) {
            errno = ENOENT;
            return -1;
        }
        FindClose(hFind);
        nResult = xx_crt_stat_fill(pStat, pPath, data.dwFileAttributes, data.nFileSizeHigh, data.nFileSizeLow, data.ftCreationTime, data.ftLastAccessTime,
                                   data.ftLastWriteTime);
        if (nResult != 0) xx_crt_memset(pStat, 0, sizeof(*pStat));
        return nResult;
    }
}

int __cdecl _stat64i32(const char *pPath, struct _stat64i32 *pStat)
{
    wchar_t *pWide = xx_crt_narrow_to_wide(pPath);
    int nResult;
    if (!pWide) {
        errno = pPath ? ENOENT : EINVAL;
        return -1;
    }
    nResult = _wstat64i32(pWide, pStat);
    free(pWide);
    return nResult;
}

int __cdecl _mkdir(const char *pPath)
{
    wchar_t *pWide = xx_crt_narrow_to_wide(pPath);
    int nResult = 0;
    if (!pWide) {
        errno = EINVAL;
        return -1;
    }
    if (!CreateDirectoryW(pWide, NULL)) {
        errno = xx_crt_errno_from_oserror(GetLastError());
        nResult = -1;
    }
    free(pWide);
    return nResult;
}

int __cdecl _rmdir(const char *pPath)
{
    wchar_t *pWide = xx_crt_narrow_to_wide(pPath);
    int nResult = 0;
    if (!pWide) {
        errno = EINVAL;
        return -1;
    }
    if (!RemoveDirectoryW(pWide)) {
        errno = xx_crt_errno_from_oserror(GetLastError());
        nResult = -1;
    }
    free(pWide);
    return nResult;
}

int __cdecl remove(const char *pPath)
{
    wchar_t *pWide = xx_crt_narrow_to_wide(pPath);
    int nResult = 0;
    if (!pWide) {
        errno = EINVAL;
        return -1;
    }
    if (!DeleteFileW(pWide)) {
        errno = xx_crt_errno_from_oserror(GetLastError());
        nResult = -1;
    }
    free(pWide);
    return nResult;
}

int __cdecl _unlink(const char *pPath)
{
    return remove(pPath);
}

int __cdecl _getpid(void)
{
    return (int)GetCurrentProcessId();
}

#endif /* _WIN32 && _MSC_VER */
