/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_scan.c
 * @brief sscanf with the conversions and integer behaviour of the
 *        Universal CRT.
 *
 * Integers are read as an unsigned 64-bit magnitude that saturates at
 * 2^64-1, negated for a minus sign, and stored truncated to the target
 * width (so %d of 99999999999 stores its low 32 bits, as UCRT does).
 * "0x" with no hexadecimal digit after it is a matching failure for %i and
 * %x. The result is the number of assigned fields, or EOF when the input
 * ends before the first conversion.
 */

#include "xxfclib/crt/xx_crt.h"

#include <stdarg.h>
#include <stdint.h>
#include <wchar.h>

static int xx_crt_scan_space(int nChar)
{
    return nChar == ' ' || (nChar >= '\t' && nChar <= '\r');
}

static int xx_crt_scan_digit(int nChar)
{
    if (nChar >= '0' && nChar <= '9') return nChar - '0';
    if (nChar >= 'a' && nChar <= 'z') return nChar - 'a' + 10;
    if (nChar >= 'A' && nChar <= 'Z') return nChar - 'A' + 10;
    return 99;
}

static int xx_crt_scan_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

/* Matches the literal pWord (lower case) at s[*pAt]; 0 matched, -1 the text
 * ended inside it, 1 a different character. */
static int xx_crt_scan_word(const char *s, long n, long *pAt, const char *pWord)
{
    long i = *pAt;
    for (; *pWord; ++pWord, ++i) {
        if (i >= n) return -1;
        if (xx_crt_scan_lower(s[i]) != *pWord) return 1;
    }
    *pAt = i;
    return 0;
}

/* A character that some floating-point form can contain. */
static int xx_crt_scan_float_char(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '+' || c == '-' || c == '(' || c == ')' ||
           c == '_';
}

/* The extent of the floating-point number UCRT's scanner accepts at the
 * start of s (n characters available): 0 and *pAccepted when complete, -1
 * when the text ends inside an incomplete form, 1 when a character makes it
 * invalid (UCRT cannot back up to the last complete prefix as strtod does). */
static int xx_crt_scan_float_extent(const char *s, long n, long *pAccepted)
{
    long i = 0;
    int nResult;
    if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
    if (i >= n) return -1;
    if (xx_crt_scan_lower(s[i]) == 'i') {
        if ((nResult = xx_crt_scan_word(s, n, &i, "inf")) != 0) return nResult;
        if (i < n && xx_crt_scan_lower(s[i]) == 'i') {
            if ((nResult = xx_crt_scan_word(s, n, &i, "inity")) != 0) return nResult;
        }
        *pAccepted = i;
        return 0;
    }
    if (xx_crt_scan_lower(s[i]) == 'n') {
        if ((nResult = xx_crt_scan_word(s, n, &i, "nan")) != 0) return nResult;
        if (i < n && s[i] == '(') {
            ++i;
            while (i < n && ((s[i] >= '0' && s[i] <= '9') || (xx_crt_scan_lower(s[i]) >= 'a' && xx_crt_scan_lower(s[i]) <= 'z') || s[i] == '_')) ++i;
            if (i >= n) return -1;
            if (s[i] != ')') return 1;
            ++i;
        }
        *pAccepted = i;
        return 0;
    }
    {
        int bHex = 0;
        int nDigits = 0;
        if (s[i] == '0' && i + 1 < n && xx_crt_scan_lower(s[i + 1]) == 'x') {
            bHex = 1;
            i += 2;
        }
        while (i < n && xx_crt_scan_digit((unsigned char)s[i]) < (bHex ? 16 : 10)) {
            ++i;
            ++nDigits;
        }
        if (i < n && s[i] == '.') {
            ++i;
            while (i < n && xx_crt_scan_digit((unsigned char)s[i]) < (bHex ? 16 : 10)) {
                ++i;
                ++nDigits;
            }
        }
        if (nDigits == 0) return i >= n ? -1 : 1;
        if (i < n && xx_crt_scan_lower(s[i]) == (bHex ? 'p' : 'e')) {
            ++i;
            if (i >= n) return -1;
            if (s[i] == '+' || s[i] == '-') ++i;
            if (i >= n) return -1;
            if (s[i] < '0' || s[i] > '9') return 1;
            while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
        }
        *pAccepted = i;
        return 0;
    }
}

enum {
    XX_CRT_SCAN_INT,
    XX_CRT_SCAN_CHAR,
    XX_CRT_SCAN_SHORT,
    XX_CRT_SCAN_LONG,
    XX_CRT_SCAN_64,
    XX_CRT_SCAN_DOUBLE_LONG,
    XX_CRT_SCAN_WIDE
};

static void xx_crt_scan_store_int(void *pTarget, int nLength, uint64_t nValue)
{
    switch (nLength) {
        case XX_CRT_SCAN_CHAR: *(unsigned char *)pTarget = (unsigned char)nValue; break;
        case XX_CRT_SCAN_SHORT: *(unsigned short *)pTarget = (unsigned short)nValue; break;
        case XX_CRT_SCAN_64: *(uint64_t *)pTarget = nValue; break;
        case XX_CRT_SCAN_LONG: *(unsigned long *)pTarget = (unsigned long)nValue; break;
        default: *(unsigned int *)pTarget = (unsigned int)nValue; break;
    }
}

int xx_crt_vsscanf(const char *pInput, const char *pFormat, int bSecure, va_list args)
{
    const unsigned char *in = (const unsigned char *)pInput;
    const unsigned char *f = (const unsigned char *)pFormat;
    int nAssigned = 0;

    while (*f) {
        int bSuppress = 0;
        long nWidth = -1;
        int nLength = XX_CRT_SCAN_INT;
        unsigned char nConversion;

        if (xx_crt_scan_space(*f)) {
            while (xx_crt_scan_space(*f)) ++f;
            while (xx_crt_scan_space(*in)) ++in;
            continue;
        }
        if (*f != '%') {
            if (*in == 0) return nAssigned ? nAssigned : -1;
            if (*in != *f) return nAssigned;
            ++in;
            ++f;
            continue;
        }
        ++f;
        if (*f == '%') {
            /* UCRT matches %% like a literal '%': no white space is skipped */
            if (*in == 0) return nAssigned ? nAssigned : -1;
            if (*in != '%') return nAssigned;
            ++in;
            ++f;
            continue;
        }
        if (*f == '*') {
            bSuppress = 1;
            ++f;
        }
        if (*f >= '0' && *f <= '9') {
            nWidth = 0;
            while (*f >= '0' && *f <= '9') {
                nWidth = nWidth * 10 + (*f - '0');
                ++f;
            }
            if (nWidth == 0) nWidth = -1;
        }
        if (f[0] == 'h') {
            if (f[1] == 'h') {
                nLength = XX_CRT_SCAN_CHAR;
                f += 2;
            } else {
                nLength = XX_CRT_SCAN_SHORT;
                ++f;
            }
        } else if (f[0] == 'l') {
            if (f[1] == 'l') {
                nLength = XX_CRT_SCAN_64;
                f += 2;
            } else {
                nLength = XX_CRT_SCAN_LONG;
                ++f;
            }
        } else if (f[0] == 'L') {
            nLength = XX_CRT_SCAN_DOUBLE_LONG;
            ++f;
        } else if (f[0] == 'j' || f[0] == 'z' || f[0] == 't') {
            nLength = sizeof(size_t) == 8 ? XX_CRT_SCAN_64 : XX_CRT_SCAN_INT;
            ++f;
        } else if (f[0] == 'I') {
            if (f[1] == '6' && f[2] == '4') {
                nLength = XX_CRT_SCAN_64;
                f += 3;
            } else if (f[1] == '3' && f[2] == '2') {
                nLength = XX_CRT_SCAN_INT;
                f += 3;
            } else {
                nLength = sizeof(void *) == 8 ? XX_CRT_SCAN_64 : XX_CRT_SCAN_INT;
                ++f;
            }
        } else if (f[0] == 'w') {
            nLength = XX_CRT_SCAN_WIDE;
            ++f;
        }
        nConversion = *f;
        if (!nConversion) break;
        ++f;

        if (nConversion == 'n') {
            if (!bSuppress) {
                xx_crt_scan_store_int(va_arg(args, void *), nLength == XX_CRT_SCAN_64 ? XX_CRT_SCAN_64 : nLength,
                                      (uint64_t)(in - (const unsigned char *)pInput));
            }
            continue;
        }
        if (nConversion != 'c' && nConversion != '[') {
            while (xx_crt_scan_space(*in)) ++in;
        }
        if (*in == 0) return nAssigned ? nAssigned : -1;

        switch (nConversion) {
            case 'd':
            case 'i':
            case 'u':
            case 'o':
            case 'x':
            case 'X':
            case 'p': {
                int nBase = nConversion == 'o' ? 8 : (nConversion == 'x' || nConversion == 'X' || nConversion == 'p') ? 16 : nConversion == 'i' ? 0 : 10;
                long nLeft = nWidth < 0 ? 0x7FFFFFFFL : nWidth;
                int bNegative = 0;
                int bAny = 0;
                int bOverflow = 0;
                uint64_t nValue = 0;
                int bSigned = nConversion == 'd' || nConversion == 'i';
                if (nConversion == 'p') nLength = sizeof(void *) == 8 ? XX_CRT_SCAN_64 : XX_CRT_SCAN_INT;
                if (nLeft && (*in == '+' || *in == '-')) {
                    bNegative = *in == '-';
                    ++in;
                    --nLeft;
                    /* the input ending inside the conversion is an input failure */
                    if (*in == 0) return nAssigned ? nAssigned : -1;
                }
                if (nLeft && *in == '0') {
                    if ((nBase == 0 || nBase == 16) && nLeft >= 2 && (in[1] == 'x' || in[1] == 'X')) {
                        /* "0x" commits to hexadecimal: the input ending there is an
                         * input failure, any other non-digit a matching failure */
                        if (nLeft >= 3 && in[2] == 0) return nAssigned ? nAssigned : -1;
                        if (nLeft < 3 || xx_crt_scan_digit(in[2]) >= 16) return nAssigned;
                        in += 2;
                        nLeft -= 2;
                        nBase = 16;
                    } else if (nBase == 0) {
                        nBase = 8;
                    }
                }
                if (nBase == 0) nBase = 10;
                while (nLeft && xx_crt_scan_digit(*in) < nBase) {
                    unsigned nDigit = (unsigned)xx_crt_scan_digit(*in);
                    if (!bOverflow) {
                        if (nValue > (UINT64_MAX - nDigit) / (unsigned)nBase) bOverflow = 1;
                        else nValue = nValue * (unsigned)nBase + nDigit;
                    }
                    bAny = 1;
                    ++in;
                    --nLeft;
                }
                if (!bAny) return nAssigned;
                if (bSigned) {
                    /* strtoll: saturate to the 64-bit range, then narrow */
                    if (bNegative) {
                        nValue = (bOverflow || nValue > (uint64_t)INT64_MAX + 1u) ? (uint64_t)INT64_MIN : 0 - nValue;
                    } else if (bOverflow || nValue > (uint64_t)INT64_MAX) {
                        nValue = (uint64_t)INT64_MAX;
                    }
                } else {
                    /* strtoull: saturate the magnitude, a minus sign wraps */
                    if (bOverflow) nValue = UINT64_MAX;
                    else if (bNegative) nValue = 0 - nValue;
                }
                if (!bSuppress) {
                    xx_crt_scan_store_int(va_arg(args, void *), nLength, nValue);
                    ++nAssigned;
                }
                break;
            }
            case 'c':
            case 'C': {
                long nCount = nWidth < 0 ? 1 : nWidth;
                int bWide = nConversion == 'C' ? nLength != XX_CRT_SCAN_SHORT : (nLength == XX_CRT_SCAN_LONG || nLength == XX_CRT_SCAN_WIDE);
                char *pTarget = NULL;
                wchar_t *pWide = NULL;
                unsigned nCapacity = 0xFFFFFFFFu;
                long i;
                if (!bSuppress) {
                    if (bWide) pWide = va_arg(args, wchar_t *);
                    else pTarget = va_arg(args, char *);
                    if (bSecure) nCapacity = va_arg(args, unsigned);
                }
                for (i = 0; i < nCount; ++i) {
                    if (*in == 0) break;
                    if (!bSuppress) {
                        if ((unsigned long)i >= nCapacity) return nAssigned;
                        if (bWide) pWide[i] = (wchar_t)*in;
                        else pTarget[i] = (char)*in;
                    }
                    ++in;
                }
                if (i == 0) return nAssigned;
                if (!bSuppress) ++nAssigned;
                break;
            }
            case 's':
            case 'S':
            case '[': {
                unsigned char aSet[256];
                int bWide = nConversion == 'S' ? nLength != XX_CRT_SCAN_SHORT : (nLength == XX_CRT_SCAN_LONG || nLength == XX_CRT_SCAN_WIDE);
                char *pTarget = NULL;
                wchar_t *pWide = NULL;
                unsigned nCapacity = 0xFFFFFFFFu;
                long nLeft = nWidth < 0 ? 0x7FFFFFFFL : nWidth;
                long nCount = 0;
                int i;
                if (nConversion == '[') {
                    int bInvert = 0;
                    for (i = 0; i < 256; ++i) aSet[i] = 0;
                    if (*f == '^') {
                        bInvert = 1;
                        ++f;
                    }
                    if (*f == ']') {
                        aSet[']'] = 1;
                        ++f;
                    }
                    while (*f && *f != ']') {
                        if (f[1] == '-' && f[2] && f[2] != ']') {
                            unsigned nFrom = f[0], nTo = f[2], c;
                            if (nFrom > nTo) {
                                unsigned t = nFrom;
                                nFrom = nTo;
                                nTo = t;
                            }
                            for (c = nFrom; c <= nTo; ++c) aSet[c] = 1;
                            f += 3;
                        } else {
                            aSet[*f] = 1;
                            ++f;
                        }
                    }
                    if (*f == ']') ++f;
                    if (bInvert) {
                        for (i = 0; i < 256; ++i) aSet[i] = (unsigned char)!aSet[i];
                    }
                    aSet[0] = 0;
                } else {
                    for (i = 0; i < 256; ++i) aSet[i] = (unsigned char)(i != 0 && !xx_crt_scan_space(i));
                }
                if (!bSuppress) {
                    if (bWide) pWide = va_arg(args, wchar_t *);
                    else pTarget = va_arg(args, char *);
                    if (bSecure) nCapacity = va_arg(args, unsigned);
                }
                while (nLeft && aSet[*in]) {
                    if (!bSuppress) {
                        if ((unsigned long)nCount + 1 >= nCapacity) {
                            if (bWide) pWide[0] = 0;
                            else pTarget[0] = 0;
                            return nAssigned;
                        }
                        if (bWide) pWide[nCount] = (wchar_t)*in;
                        else pTarget[nCount] = (char)*in;
                    }
                    ++nCount;
                    ++in;
                    --nLeft;
                }
                if (nCount == 0) return nAssigned;
                if (!bSuppress) {
                    if (bWide) pWide[nCount] = 0;
                    else pTarget[nCount] = 0;
                    ++nAssigned;
                }
                break;
            }
            case 'e':
            case 'E':
            case 'f':
            case 'F':
            case 'g':
            case 'G':
            case 'a':
            case 'A': {
                /* The number is examined in place, however long it is: the
                 * candidate is the run of characters a floating-point form
                 * can contain (cut at the field width), and the accepted
                 * prefix is converted with the length-bounded strtod/strtof,
                 * the latter directly to float (one rounding, as UCRT). */
                const char *pText = (const char *)in;
                long nCandidate = 0;
                char *pEnd = NULL;
                int nErrno = 0;
                int bDouble = nLength == XX_CRT_SCAN_LONG || nLength == XX_CRT_SCAN_DOUBLE_LONG || nLength == XX_CRT_SCAN_64;
                while ((nWidth < 0 || nCandidate < nWidth) && xx_crt_scan_float_char(pText[nCandidate])) ++nCandidate;
                {
                    /* UCRT's scanner reads the number greedily and cannot back
                     * up: stopping inside an incomplete form ("5e", "-",
                     * "infin", "nan(x", "0x") is an input failure when the
                     * input ends there and a matching failure otherwise. */
                    long nAccepted = 0;
                    int nState = xx_crt_scan_float_extent(pText, nCandidate, &nAccepted);
                    if (nState != 0) {
                        if (nState < 0 && pText[nCandidate] == 0) return nAssigned ? nAssigned : -1;
                        return nAssigned;
                    }
                    nCandidate = nAccepted;
                }
                if (bDouble) {
                    double nValue = xx_crt_strtod_n(pText, (size_t)nCandidate, &pEnd, &nErrno);
                    if (pEnd == pText) return nAssigned;
                    if (!bSuppress) {
                        *va_arg(args, double *) = nValue;
                        ++nAssigned;
                    }
                } else {
                    float nValue = xx_crt_strtof_n(pText, (size_t)nCandidate, &pEnd, &nErrno);
                    if (pEnd == pText) return nAssigned;
                    if (!bSuppress) {
                        *va_arg(args, float *) = nValue;
                        ++nAssigned;
                    }
                }
                in += pEnd - pText;
                break;
            }
            default:
                return nAssigned;
        }
    }
    return nAssigned;
}
