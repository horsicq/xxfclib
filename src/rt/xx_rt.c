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

/* xx_rt.c - process, I/O, formatting and clock runtime services.
 *
 * On Windows the implementation uses the Win32 API: CreateFile/ReadFile for
 * I/O, WriteFile to the standard handles for output, ExitProcess and
 * GetEnvironmentVariableA. The remaining runtime primitives (sorting, numeric
 * conversion and integer formatting) are implemented here rather than taken
 * from the CRT. Runtime memory primitives live in src/io/xx_io.c,
 * narrow/wide string primitives live in src/strings/xx_string.c, and the
 * Win32 UTF conversion boundary lives in
 * src/strings/platforms/xx_string_windows.c.
 *
 * Elsewhere the C standard library is used directly.
 *
 * The remaining pieces live next door and are equally CRT-free:
 * xx_rt_math.c (the math functions), xx_rt_fp.c (double <-> decimal string)
 * and xx_entry_windows.c (startup, argv, the compiler support routines).
 *
 * The formatter below spells %e, %f and %g out of xx_rt_dtoa_cformat, so a
 * diagnostic that prints a double reads the same on a CRT-free Windows build
 * as it does anywhere else. %a has no digit generator here and no caller in
 * the project; it keeps a marker.
 */

/* The hosted half of the clock below is POSIX, and the project compiles as
 * strict ISO C99 (CMAKE_C_EXTENSIONS OFF), which hides clock_gettime and
 * CLOCK_MONOTONIC without this. It has to precede the first include.       */

#include "xxfclib/rt/xx_rt.h"
#include "platforms/xx_rt_platform.h"


/* ------------------------------------------------------------------------ */
/*  Process                                                                  */
/* ------------------------------------------------------------------------ */



static void xx_rt_swap_bytes(char *pLeft, char *pRight, size_t nSize)
{
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        char nTemp = pLeft[i];

        pLeft[i] = pRight[i];
        pRight[i] = nTemp;
    }
}

static void xx_rt_heap_sift_down(char *pBase, size_t nRoot, size_t nEnd,
                                 size_t nSize,
                                 xx_rt_compare_context_fn fnCompare,
                                 void *pContext)
{
    while (nRoot <= (nEnd - 1) / 2) {
        size_t nChild = nRoot * 2 + 1;
        size_t nSwap = nRoot;

        if (fnCompare(pBase + nSwap * nSize,
                      pBase + nChild * nSize, pContext) < 0) {
            nSwap = nChild;
        }

        if ((nChild < nEnd) &&
            (fnCompare(pBase + nSwap * nSize,
                       pBase + (nChild + 1) * nSize, pContext) < 0)) {
            nSwap = nChild + 1;
        }

        if (nSwap == nRoot) {
            return;
        }

        xx_rt_swap_bytes(pBase + nRoot * nSize,
                         pBase + nSwap * nSize, nSize);
        nRoot = nSwap;
    }
}

/* In-place heapsort has bounded O(log n) index state, never allocates, and
 * cannot degrade to quadratic behavior for equal or adversarial keys. */
static void xx_rt_qsort_context_impl(char *pBase, size_t nCount,
                                     size_t nSize,
                                     xx_rt_compare_context_fn fnCompare,
                                     void *pContext)
{
    size_t nStart = nCount / 2;
    size_t nEnd = nCount - 1;

    while (nStart != 0) {
        nStart--;
        xx_rt_heap_sift_down(pBase, nStart, nEnd, nSize,
                             fnCompare, pContext);
    }

    while (nEnd != 0) {
        xx_rt_swap_bytes(pBase, pBase + nEnd * nSize, nSize);
        nEnd--;
        if (nEnd != 0) {
            xx_rt_heap_sift_down(pBase, 0, nEnd, nSize,
                                 fnCompare, pContext);
        }
    }
}



void xx_rt_qsort_context(void *pBase, size_t nCount, size_t nSize,
                         xx_rt_compare_context_fn fnCompare,
                         void *pContext)
{
    if ((pBase == NULL) || (fnCompare == NULL) || (nCount < 2) ||
        (nSize == 0) || (nCount > ((size_t)-1) / nSize)) {
        return;
    }

    xx_rt_qsort_context_impl((char *)pBase, nCount, nSize,
                             fnCompare, pContext);
}

/* ------------------------------------------------------------------------ */
/*  Conversion                                                               */
/* ------------------------------------------------------------------------ */

/* xx_rt_strtod lives in utils_fp.c, which converts without the CRT. */

static int xx_rt_is_space_char(char nChar)
{
    return ((nChar == 0x20) || (nChar == 0x09) || (nChar == 0x0A) || (nChar == 0x0D) || (nChar == 0x0C) || (nChar == 0x0B)) ? 1 : 0;
}

static int xx_rt_digit_value(char nChar, int nBase)
{
    int nValue = -1;

    if ((nChar >= '0') && (nChar <= '9')) {
        nValue = nChar - '0';
    } else if ((nChar >= 'a') && (nChar <= 'z')) {
        nValue = nChar - 'a' + 10;
    } else if ((nChar >= 'A') && (nChar <= 'Z')) {
        nValue = nChar - 'A' + 10;
    }

    if ((nValue < 0) || (nValue >= nBase)) {
        return -1;
    }

    return nValue;
}

/* Accumulates the digits and reports overflow instead of wrapping, so the
 * callers can saturate the way the C library does. */
static unsigned long long xx_rt_parse_integer(const char *pString, const char **ppEnd, int nBase, int *pbNegative, int *pbOverflow)
{
    const unsigned long long nUnsignedMax = ~0ull;
    const char *p = pString;
    unsigned long long nResult = 0;
    int bAny = 0;

    *pbNegative = 0;
    *pbOverflow = 0;

    while (xx_rt_is_space_char(*p)) {
        p++;
    }

    if ((*p == '+') || (*p == '-')) {
        *pbNegative = (*p == '-') ? 1 : 0;
        p++;
    }

    if ((nBase == 0) || (nBase == 16)) {
        if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X'))) {
            p += 2;
            nBase = 16;
        } else if (nBase == 0) {
            nBase = (p[0] == '0') ? 8 : 10;
        }
    }

    for (;;) {
        int nDigit = xx_rt_digit_value(*p, nBase);

        if (nDigit < 0) {
            break;
        }

        if (nResult > ((nUnsignedMax - (unsigned long long)nDigit) / (unsigned long long)nBase)) {
            *pbOverflow = 1;
        } else {
            nResult = nResult * (unsigned long long)nBase + (unsigned long long)nDigit;
        }

        bAny = 1;
        p++;
    }

    if (ppEnd) {
        *ppEnd = bAny ? p : pString;
    }

    return nResult;
}

long xx_rt_strtol(const char *pString, const char **ppEnd, int nBase)
{
    /* LONG_MAX without <limits.h>: the header is not part of the wrapped set. */
    const unsigned long long nLongMax = (unsigned long long)((unsigned long)-1 >> 1);
    int bNegative = 0;
    int bOverflow = 0;
    unsigned long long nValue = xx_rt_parse_integer(pString, ppEnd, nBase, &bNegative, &bOverflow);

    if (bNegative) {
        if (bOverflow || (nValue > (nLongMax + 1ull))) {
            nValue = nLongMax + 1ull;
        }

        /* The magnitude of LONG_MIN has no positive long, so the negation is
         * done on the unsigned type. */
        return (long)(0ul - (unsigned long)nValue);
    }

    if (bOverflow || (nValue > nLongMax)) {
        nValue = nLongMax;
    }

    return (long)nValue;
}

unsigned long long xx_rt_strtoull(const char *pString, const char **ppEnd, int nBase)
{
    int bNegative = 0;
    int bOverflow = 0;
    unsigned long long nValue = xx_rt_parse_integer(pString, ppEnd, nBase, &bNegative, &bOverflow);

    if (bOverflow) {
        return ~0ull; /* ULLONG_MAX, as strtoull() saturates */
    }

    return bNegative ? (unsigned long long)(0ull - nValue) : nValue;
}


/* ------------------------------------------------------------------------ */
/*  I/O                                                                      */
/* ------------------------------------------------------------------------ */


/* ------------------------------------------------------------------------ */
/*  Formatting                                                               */
/* ------------------------------------------------------------------------ */


XX_RT_PRINTF_LIKE(3, 4) int xx_rt_snprintf(char *pBuffer, size_t nSize, const char *pFormat, ...)
{
    XX_RT_VA_LIST args;
    int nResult = 0;

    XX_RT_VA_START(args, pFormat);
    nResult = xx_rt_vsnprintf(pBuffer, nSize, pFormat, args);
    XX_RT_VA_END(args);

    return nResult;
}

/* Formatted output goes through xx_rt_vsnprintf and then one write, so the
 * Windows path never touches the CRT stdio layer.                          */
XX_RT_PRINTF_LIKE(2, 0) static int xx_rt_vfprintf_stream(void *pStream, const char *pFormat, XX_RT_VA_LIST args)
{
    char sStack[1024];
    XX_RT_VA_LIST copy;
    int nSize = 0;

    XX_RT_VA_COPY(copy, args);
    nSize = xx_rt_vsnprintf(sStack, sizeof(sStack), pFormat, copy);
    XX_RT_VA_END(copy);

    if (nSize < 0) {
        return -1;
    }

    if ((size_t)nSize < sizeof(sStack)) {
        return xx_rt_platform_write_stream(pStream, sStack, (size_t)nSize);
    }

    {
        char *pHeap = (char *)xx_rt_malloc((size_t)nSize + 1);
        int nResult = 0;

        if (pHeap == NULL) {
            return -1;
        }

        xx_rt_vsnprintf(pHeap, (size_t)nSize + 1, pFormat, args);
        nResult = xx_rt_platform_write_stream(pStream, pHeap, (size_t)nSize);
        xx_rt_free(pHeap);

        return nResult;
    }
}

XX_RT_PRINTF_LIKE(1, 2) int xx_rt_printf(const char *pFormat, ...)
{
    XX_RT_VA_LIST args;
    int nResult = 0;

    XX_RT_VA_START(args, pFormat);
    nResult = xx_rt_vfprintf_stream(xx_rt_stdout(), pFormat, args);
    XX_RT_VA_END(args);

    return nResult;
}

XX_RT_PRINTF_LIKE(2, 3) int xx_rt_fprintf(void *pStream, const char *pFormat, ...)
{
    XX_RT_VA_LIST args;
    int nResult = 0;

    XX_RT_VA_START(args, pFormat);
    nResult = xx_rt_vfprintf_stream(pStream, pFormat, args);
    XX_RT_VA_END(args);

    return nResult;
}

/* ------------------------------------------------------------------------ */
/*  Clock                                                                    */
/* ------------------------------------------------------------------------ */


/* Math lives in utils_math.c; double/decimal conversion in utils_fp.c. */
