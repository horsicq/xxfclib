/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_rt_windows.c
 * @brief Win32 runtime implementations. These call kernel32 directly, which is what lets the shared target link with /NODEFAULTLIB.
 *
 * Split out of xx_rt.c, which keeps the portable remainder. Every function
 * here is defined under its public xx_rt_ name with no wrapper layer, the
 * shape xx_rt_utf8_to_utf16 already uses in the string platform files.
 */

#if defined(_WIN32)

#include "xxfclib/rt/xx_rt.h"
#include "xx_rt_platform.h"

#include <windows.h>

XX_RT_NORETURN void xx_rt_exit(int nCode)
{
    ExitProcess((UINT)nCode);
}

char *xx_rt_getenv(const char *pName)
{
    /* getenv() hands back a pointer that stays valid, so the value is kept
     * in a static buffer to preserve that contract.                         */
    static char sValue[4096];
    DWORD nSize = GetEnvironmentVariableA(pName, sValue, (DWORD)sizeof(sValue));

    if ((nSize == 0) || (nSize >= sizeof(sValue))) {
        return NULL;
    }

    return sValue;
}

typedef struct xx_rt_plain_sort_context_s {
    xx_rt_compare_fn fnCompare;
} xx_rt_plain_sort_context;

static int xx_rt_plain_sort_compare(const void *pLeft, const void *pRight,
                                    void *pContext)
{
    xx_rt_plain_sort_context *pSort =
        (xx_rt_plain_sort_context *)pContext;

    return pSort->fnCompare(pLeft, pRight);
}

void xx_rt_qsort(void *pBase, size_t nCount, size_t nSize,
                 xx_rt_compare_fn fnCompare)
{
    if ((pBase == NULL) || (fnCompare == NULL) || (nCount < 2) ||
        (nSize == 0) || (nCount > ((size_t)-1) / nSize)) {
        return;
    }

    {
        xx_rt_plain_sort_context context;

        context.fnCompare = fnCompare;
        /* The heapsort itself is static in xx_rt.c, so this goes through
         * the public wrapper rather than reaching across the file. */
        xx_rt_qsort_context(pBase, nCount, nSize,
                            xx_rt_plain_sort_compare, &context);
    }
}

int xx_rt_rand(void)
{
    /* xorshift32; Math.random() is not used by the signature database, so a
     * small deterministic generator is enough.                              */
    static unsigned int nState = 0;

    if (nState == 0) {
        nState = (unsigned int)GetTickCount() | 1u;
    }

    nState ^= nState << 13;
    nState ^= nState >> 17;
    nState ^= nState << 5;

    return (int)(nState % (XX_RT_RAND_MAX + 1));
}


/* Stream handles are the Win32 standard handles or a CreateFile handle. */
void *xx_rt_stdout(void)
{
    return (void *)GetStdHandle(STD_OUTPUT_HANDLE);
}

void *xx_rt_stderr(void)
{
    return (void *)GetStdHandle(STD_ERROR_HANDLE);
}

int xx_rt_platform_write_stream(void *pStream, const char *pData, size_t nSize)
{
    DWORD nWritten = 0;

    if ((pStream == NULL) || (pStream == INVALID_HANDLE_VALUE) || (nSize == 0)) {
        return 0;
    }

    if (!WriteFile((HANDLE)pStream, pData, (DWORD)nSize, &nWritten, NULL)) {
        return -1;
    }

    return (int)nWritten;
}

void *xx_rt_fopen(const char *pFileName, const char *pMode)
{
    int bWrite = ((pMode != NULL) && ((pMode[0] == 'w') || (pMode[0] == 'a'))) ? 1 : 0;
    WCHAR *pWide = xx_rt_utf8_to_utf16(pFileName);
    HANDLE hFile = INVALID_HANDLE_VALUE;

    if (pWide == NULL) {
        return NULL;
    }

    hFile = CreateFileW(pWide, bWrite ? GENERIC_WRITE : GENERIC_READ, bWrite ? 0 : (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL,
                        bWrite ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    xx_rt_free(pWide);

    if (hFile == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    return (void *)hFile;
}

int xx_rt_fclose(void *pFile)
{
    if (pFile == NULL) {
        return -1;
    }

    return CloseHandle((HANDLE)pFile) ? 0 : -1;
}

int xx_rt_remove(const char *pFileName)
{
    WCHAR *pWide = xx_rt_utf8_to_utf16(pFileName);
    int nResult;

    if (pWide == NULL) {
        return -1;
    }

    nResult = DeleteFileW(pWide) ? 0 : -1;
    xx_rt_free(pWide);

    return nResult;
}

size_t xx_rt_fread(void *pBuffer, size_t nSize, size_t nCount, void *pFile)
{
    size_t nTotal = nSize * nCount;
    size_t nDone = 0;

    if ((pFile == NULL) || (nSize == 0) || (nTotal == 0)) {
        return 0;
    }

    /* ReadFile counts in a DWORD, so a request of 4 GiB or more is split
     * into chunks rather than truncated by the cast. */
    while (nDone < nTotal) {
        size_t nChunk = nTotal - nDone;
        DWORD nRead = 0;

        if (nChunk > 0x40000000u) {
            nChunk = 0x40000000u;
        }

        if (!ReadFile((HANDLE)pFile, (char *)pBuffer + nDone, (DWORD)nChunk, &nRead, NULL)) {
            break;
        }

        nDone += (size_t)nRead;

        if (nRead < (DWORD)nChunk) {
            break; /* end of file */
        }
    }

    return nDone / nSize;
}

int xx_rt_fseek(void *pFile, long long nOffset, int nOrigin)
{
    LARGE_INTEGER liDistance;
    DWORD nMethod = FILE_BEGIN;

    if (pFile == NULL) {
        return -1;
    }

    if (nOrigin == XX_RT_SEEK_CUR) {
        nMethod = FILE_CURRENT;
    } else if (nOrigin == XX_RT_SEEK_END) {
        nMethod = FILE_END;
    }

    liDistance.QuadPart = (LONGLONG)nOffset;

    return SetFilePointerEx((HANDLE)pFile, liDistance, NULL, nMethod) ? 0 : -1;
}

long long xx_rt_ftell(void *pFile)
{
    LARGE_INTEGER liZero;
    LARGE_INTEGER liPosition;

    if (pFile == NULL) {
        return -1;
    }

    liZero.QuadPart = 0;
    liPosition.QuadPart = 0;

    if (!SetFilePointerEx((HANDLE)pFile, liZero, &liPosition, FILE_CURRENT)) {
        return -1;
    }

    return (long long)liPosition.QuadPart;
}

void xx_rt_rewind(void *pFile)
{
    xx_rt_fseek(pFile, 0, XX_RT_SEEK_SET);
}

int xx_rt_fflush(void *pStream)
{
    /* WriteFile is unbuffered, so there is nothing to flush for the standard
     * handles; a real file handle gets a genuine flush.                     */
    if ((pStream == NULL) || (pStream == xx_rt_stdout()) || (pStream == xx_rt_stderr())) {
        return 0;
    }

    return FlushFileBuffers((HANDLE)pStream) ? 0 : -1;
}



/* A printf subset covering everything cdie uses: %s %c %d %i %u %o %x %X %p
 * %e %E %f %F %g %G %% with the -, +, space, 0 and # flags, a width, a
 * precision, and the h, hh, l, ll and z length modifiers.
 *
 * The floating point conversions are laid out here and generate their digits
 * in xx_rt_dtoa_cformat; %a is the one specifier without a generator, and the
 * only one that still prints a marker. Everything cdie itself prints as a
 * number still goes through xx_rt_dtoa_shortest / xx_rt_dtoa_fixed /
 * xx_rt_dtoa_precision - these exist so a diagnostic that reaches for %g reads
 * the same here as it does on a hosted build.                              */

typedef struct {
    char *pBuffer;
    size_t nCapacity;
    size_t nWritten; /* what would have been written, C99 style */
} XFormatSink;

static void fmt_put(XFormatSink *pSink, char nChar)
{
    if (pSink->nWritten + 1 < pSink->nCapacity) {
        pSink->pBuffer[pSink->nWritten] = nChar;
    }

    pSink->nWritten++;
}

static void fmt_pad(XFormatSink *pSink, char nChar, int nCount)
{
    int i = 0;

    for (i = 0; i < nCount; i++) {
        fmt_put(pSink, nChar);
    }
}

static void fmt_emit(XFormatSink *pSink, const char *pText, size_t nSize, int nWidth, int bLeft, int bZero)
{
    int nPad = (nWidth > (int)nSize) ? (nWidth - (int)nSize) : 0;
    size_t i = 0;

    if ((!bLeft) && nPad) {
        fmt_pad(pSink, bZero ? '0' : ' ', nPad);
    }

    for (i = 0; i < nSize; i++) {
        fmt_put(pSink, pText[i]);
    }

    if (bLeft && nPad) {
        fmt_pad(pSink, ' ', nPad);
    }
}

static int fmt_unsigned(char *pBuffer, unsigned long long nValue, unsigned int nBase, int bUpper)
{
    const char *pDigits = bUpper ? "0123456789ABCDEF" : "0123456789abcdef";
    char sTemp[72];
    int nCount = 0;
    int i = 0;

    if (nValue == 0) {
        sTemp[nCount++] = '0';
    }

    while (nValue) {
        sTemp[nCount++] = pDigits[nValue % nBase];
        nValue /= nBase;
    }

    for (i = 0; i < nCount; i++) {
        pBuffer[i] = sTemp[nCount - 1 - i];
    }

    return nCount;
}

/* Emits one integer conversion: the sign or 0x prefix, the zeros the
 * precision asks for, then the digits, the whole thing padded to the field
 * width. The precision zeros go straight into the sink, so a precision read
 * from the format or from va_arg cannot overrun the caller's digit buffer,
 * however large it is.                                                     */
static void fmt_emit_number(XFormatSink *pSink, const char *pPrefix, int nPrefix, const char *pDigits, int nDigits, int nZeros, int nWidth, int bLeft,
                            int bZero)
{
    /* nZeros comes from the precision and can be as large as an int holds,
     * so the field width is reduced by it rather than compared against a
     * sum that would overflow. */
    int nOut = nPrefix + nDigits;
    int nPad = ((nWidth > nZeros) && ((nWidth - nZeros) > nOut)) ? (nWidth - nZeros - nOut) : 0;
    int i = 0;

    if ((!bLeft) && (!bZero)) {
        fmt_pad(pSink, ' ', nPad);
    }

    for (i = 0; i < nPrefix; i++) {
        fmt_put(pSink, pPrefix[i]);
    }

    /* The zero padding of the 0 flag goes after the sign, not before it. */
    if ((!bLeft) && bZero) {
        fmt_pad(pSink, '0', nPad);
    }

    fmt_pad(pSink, '0', nZeros);

    for (i = 0; i < nDigits; i++) {
        fmt_put(pSink, pDigits[i]);
    }

    if (bLeft) {
        fmt_pad(pSink, ' ', nPad);
    }
}

XX_RT_PRINTF_LIKE(3, 0) int xx_rt_vsnprintf(char *pBuffer, size_t nSize, const char *pFormat, XX_RT_VA_LIST args)
{
    XFormatSink sink;
    const char *p = pFormat;

    sink.pBuffer = pBuffer;
    sink.nCapacity = nSize;
    sink.nWritten = 0;

    while (*p) {
        int bLeft = 0;
        int bZero = 0;
        int bPlus = 0;
        int bSpace = 0;
        int bAlt = 0;
        int nWidth = 0;
        int nPrecision = -1;
        int nLong = 0; /* 1 = l, 2 = ll, -1 = h, -2 = hh, 3 = z */

        if (*p != '%') {
            fmt_put(&sink, *p++);
            continue;
        }

        p++;

        for (;;) {
            if (*p == '-') {
                bLeft = 1;
            } else if (*p == '0') {
                bZero = 1;
            } else if (*p == '+') {
                bPlus = 1;
            } else if (*p == ' ') {
                bSpace = 1;
            } else if (*p == '#') {
                bAlt = 1;
            } else {
                break;
            }

            p++;
        }

        if (*p == '*') {
            nWidth = va_arg(args, int);
            p++;

            if (nWidth < 0) {
                bLeft = 1;
                nWidth = -nWidth;
            }
        } else {
            while ((*p >= '0') && (*p <= '9')) {
                nWidth = nWidth * 10 + (*p - '0');
                p++;
            }
        }

        if (*p == '.') {
            p++;
            nPrecision = 0;

            if (*p == '*') {
                nPrecision = va_arg(args, int);
                p++;
            } else {
                while ((*p >= '0') && (*p <= '9')) {
                    nPrecision = nPrecision * 10 + (*p - '0');
                    p++;
                }
            }
        }

        if ((p[0] == 'l') && (p[1] == 'l')) {
            nLong = 2;
            p += 2;
        } else if (*p == 'l') {
            nLong = 1;
            p++;
        } else if ((p[0] == 'h') && (p[1] == 'h')) {
            nLong = -2;
            p += 2;
        } else if (*p == 'h') {
            nLong = -1;
            p++;
        } else if (*p == 'z') {
            nLong = 3;
            p++;
        } else if (*p == 'j') {
            nLong = 2;
            p++;
        }

        switch (*p) {
            case 's': {
                const char *pText = va_arg(args, const char *);
                size_t nTextSize = 0;

                if (pText == NULL) {
                    pText = "(null)";
                }

                nTextSize = xx_rt_strlen(pText);

                if ((nPrecision >= 0) && ((size_t)nPrecision < nTextSize)) {
                    nTextSize = (size_t)nPrecision;
                }

                fmt_emit(&sink, pText, nTextSize, nWidth, bLeft, 0);
                break;
            }

            case 'c': {
                char nChar = (char)va_arg(args, int);

                fmt_emit(&sink, &nChar, 1, nWidth, bLeft, 0);
                break;
            }

            case 'd':
            case 'i': {
                long long nValue = 0;
                char sDigits[80];
                char sPrefix[2];
                int nDigits = 0;
                int nPrefix = 0;
                int nZeros = 0;
                unsigned long long nMagnitude = 0;
                int bNegative = 0;

                if (nLong == 2) {
                    nValue = va_arg(args, long long);
                } else if (nLong == 1) {
                    nValue = va_arg(args, long);
                } else if (nLong == 3) {
                    nValue = (long long)va_arg(args, size_t);
                } else {
                    nValue = va_arg(args, int);
                }

                bNegative = (nValue < 0) ? 1 : 0;
                nMagnitude = bNegative ? (unsigned long long)(-(nValue + 1)) + 1ull : (unsigned long long)nValue;
                nDigits = fmt_unsigned(sDigits, nMagnitude, 10, 0);

                /* C99 7.19.6.1: a precision of zero prints nothing for the
                 * value zero, and a larger one zero-extends the digits. */
                if ((nPrecision == 0) && (nMagnitude == 0)) {
                    nDigits = 0;
                }

                if (nPrecision > nDigits) {
                    nZeros = nPrecision - nDigits;
                }

                if (bNegative) {
                    sPrefix[nPrefix++] = '-';
                } else if (bPlus) {
                    sPrefix[nPrefix++] = '+';
                } else if (bSpace) {
                    sPrefix[nPrefix++] = ' ';
                }

                fmt_emit_number(&sink, sPrefix, nPrefix, sDigits, nDigits, nZeros, nWidth, bLeft, bZero && (nPrecision < 0));
                break;
            }

            case 'u':
            case 'o':
            case 'x':
            case 'X':
            case 'p': {
                unsigned long long nValue = 0;
                char sDigits[80];
                char sPrefix[2];
                int nDigits = 0;
                int nPrefix = 0;
                int nZeros = 0;
                unsigned int nBase = 10;
                int bUpper = (*p == 'X') ? 1 : 0;

                if (*p == 'o') {
                    nBase = 8;
                } else if ((*p == 'x') || (*p == 'X') || (*p == 'p')) {
                    nBase = 16;
                }

                if (*p == 'p') {
                    nValue = (unsigned long long)(size_t)va_arg(args, void *);
                } else if (nLong == 2) {
                    nValue = va_arg(args, unsigned long long);
                } else if (nLong == 1) {
                    nValue = va_arg(args, unsigned long);
                } else if (nLong == 3) {
                    nValue = (unsigned long long)va_arg(args, size_t);
                } else {
                    nValue = va_arg(args, unsigned int);
                }

                if (bAlt && nBase == 16 && nValue) {
                    sPrefix[nPrefix++] = '0';
                    sPrefix[nPrefix++] = bUpper ? 'X' : 'x';
                }

                nDigits = fmt_unsigned(sDigits, nValue, nBase, bUpper);

                if ((nPrecision == 0) && (nValue == 0)) {
                    nDigits = 0;
                }

                if (nPrecision > nDigits) {
                    nZeros = nPrecision - nDigits;
                }

                /* C99 7.19.6.1: # widens an octal conversion just enough to
                 * put a zero in front of it. */
                if (bAlt && (nBase == 8) && (nZeros == 0) && ((nDigits == 0) || (sDigits[0] != '0'))) {
                    nZeros = 1;
                }

                fmt_emit_number(&sink, sPrefix, nPrefix, sDigits, nDigits, nZeros, nWidth, bLeft, bZero && (nPrecision < 0));
                break;
            }

            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G': {
                double nValue = va_arg(args, double);
                /* The widest conversion is %f of a value near DBL_MAX at the
                 * precision ceiling: 309 integer digits, the point and 400
                 * fraction digits. */
                char sText[768];
                char sPrefix[2];
                int nDigits = 0;
                int nPrefix = 0;
                int bSpecial = (xx_rt_isnan(nValue) || xx_rt_isinf(nValue)) ? 1 : 0;

                /* The conversion produces the magnitude only, so the sign is
                 * a prefix like the one an integer conversion carries. */
                if (xx_rt_signbit(nValue)) {
                    sPrefix[nPrefix++] = '-';
                } else if (bPlus) {
                    sPrefix[nPrefix++] = '+';
                } else if (bSpace) {
                    sPrefix[nPrefix++] = ' ';
                }

                nDigits = xx_rt_dtoa_cformat(nValue, *p, nPrecision, bAlt, sText, sizeof(sText));

                if (nDigits > (int)(sizeof(sText) - 1)) {
                    nDigits = (int)(sizeof(sText) - 1);
                }

                /* C99 7.19.6.1: the 0 flag is ignored for an infinity or a
                 * NaN, which pad with spaces like a string. */
                fmt_emit_number(&sink, sPrefix, nPrefix, sText, nDigits, 0, nWidth, bLeft, bZero && (!bSpecial));
                break;
            }

            case 'a':
            case 'A': {
                /* Hexadecimal floating point has no generator here and no
                 * caller in the project; consume the argument so the varargs
                 * cursor stays in step and emit a marker.                   */
                (void)va_arg(args, double);
                fmt_emit(&sink, "<float>", 7, nWidth, bLeft, 0);
                break;
            }

            case '%': fmt_put(&sink, '%'); break;

            case 0: continue;

            default:
                fmt_put(&sink, '%');
                fmt_put(&sink, *p);
                break;
        }

        p++;
    }

    if (nSize) {
        size_t nTerminator = (sink.nWritten < nSize) ? sink.nWritten : (nSize - 1);

        pBuffer[nTerminator] = 0;
    }

    return (int)sink.nWritten;
}


long long xx_rt_clock_ms(void)
{
    LARGE_INTEGER nFrequency;
    LARGE_INTEGER nCounter;

    /* QueryPerformanceCounter is in KERNEL32, so the CRT-free build reaches
     * it like every other primitive in this file. */
    if (QueryPerformanceFrequency(&nFrequency) && (nFrequency.QuadPart > 0) && QueryPerformanceCounter(&nCounter)) {
        long long nTicks = (long long)nCounter.QuadPart;
        long long nPerSecond = (long long)nFrequency.QuadPart;

        /* Split into whole seconds and a remainder: scaling the raw tick
         * count by 1000 overflows after a few weeks of uptime. */
        return (nTicks / nPerSecond) * 1000 + ((nTicks % nPerSecond) * 1000) / nPerSecond;
    }

    /* Pre-Vista fallback: 32-bit milliseconds, so a reading wraps once every
     * 49 days. A difference of two readings is still right either side of
     * the wrap for every interval this is used to measure. */
    return (long long)GetTickCount();
}

#endif /* _WIN32 */
