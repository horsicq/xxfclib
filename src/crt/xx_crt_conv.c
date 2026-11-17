/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_conv.c
 * @brief strtol, strtoul, strtoll and strtoull with the Universal CRT's
 *        results, end pointers and errno.
 *
 * As in UCRT: a "0x" prefix that no hexadecimal digit follows is no number
 * at all (the end pointer stays at the start of the string); the unsigned
 * variants negate a minus-signed magnitude that fits and saturate with
 * ERANGE otherwise; the signed variants saturate to their minimum or
 * maximum with ERANGE. An invalid base (UCRT's invalid-parameter handler)
 * converts nothing and reports EINVAL.
 */

#include "xxfclib/crt/xx_crt.h"

#include <stdint.h>

static int xx_crt_conv_space(char nChar)
{
    return nChar == ' ' || (nChar >= '\t' && nChar <= '\r');
}

static int xx_crt_conv_digit(char nChar)
{
    if (nChar >= '0' && nChar <= '9') return nChar - '0';
    if (nChar >= 'a' && nChar <= 'z') return nChar - 'a' + 10;
    if (nChar >= 'A' && nChar <= 'Z') return nChar - 'A' + 10;
    return 99;
}

/* Parses the magnitude; *pbOverflow when it exceeds nLimit. Returns 0 and
 * leaves *ppEnd at pString when there is no number. */
static uint64_t xx_crt_conv_parse(const char *pString, char **ppEnd, int nBase, uint64_t nLimit, int *pbNegative, int *pbOverflow, int *pnErrno)
{
    const char *p = pString;
    uint64_t nValue = 0;
    int bAny = 0;

    *pbNegative = 0;
    *pbOverflow = 0;
    if (ppEnd) *ppEnd = (char *)pString;
    if (nBase < 0 || nBase == 1 || nBase > 36) {
        *pnErrno = XX_CRT_EINVAL;
        return 0;
    }
    while (xx_crt_conv_space(*p)) ++p;
    if (*p == '+' || *p == '-') {
        *pbNegative = *p == '-';
        ++p;
    }
    if (nBase == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            nBase = 16;
            p += 2;
        } else if (p[0] == '0') {
            nBase = 8;
        } else {
            nBase = 10;
        }
    } else if (nBase == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    for (;; ++p) {
        int nDigit = xx_crt_conv_digit(*p);
        if (nDigit >= nBase) break;
        bAny = 1;
        if (!*pbOverflow) {
            if (nValue > (nLimit - (uint64_t)nDigit) / (uint64_t)nBase) {
                *pbOverflow = 1;
            } else {
                nValue = nValue * (uint64_t)nBase + (uint64_t)nDigit;
            }
        }
    }
    if (!bAny) {
        *pbNegative = 0;
        *pbOverflow = 0;
        return 0;
    }
    if (ppEnd) *ppEnd = (char *)p;
    return nValue;
}

unsigned long long xx_crt_strtoull(const char *pString, char **ppEnd, int nBase, int *pnErrno)
{
    int bNegative, bOverflow;
    int nLocal = 0;
    uint64_t nValue;
    if (!pnErrno) pnErrno = &nLocal;
    nValue = xx_crt_conv_parse(pString, ppEnd, nBase, UINT64_MAX, &bNegative, &bOverflow, pnErrno);
    if (bOverflow) {
        *pnErrno = XX_CRT_ERANGE;
        return UINT64_MAX;
    }
    return bNegative ? (unsigned long long)(0 - nValue) : nValue;
}

long long xx_crt_strtoll(const char *pString, char **ppEnd, int nBase, int *pnErrno)
{
    int bNegative, bOverflow;
    int nLocal = 0;
    uint64_t nValue;
    if (!pnErrno) pnErrno = &nLocal;
    nValue = xx_crt_conv_parse(pString, ppEnd, nBase, UINT64_MAX, &bNegative, &bOverflow, pnErrno);
    if (bNegative) {
        if (bOverflow || nValue > (uint64_t)INT64_MAX + 1u) {
            *pnErrno = XX_CRT_ERANGE;
            return INT64_MIN;
        }
        return (long long)(0 - nValue);
    }
    if (bOverflow || nValue > (uint64_t)INT64_MAX) {
        *pnErrno = XX_CRT_ERANGE;
        return INT64_MAX;
    }
    return (long long)nValue;
}

unsigned long xx_crt_strtoul(const char *pString, char **ppEnd, int nBase, int *pnErrno)
{
    int bNegative, bOverflow;
    int nLocal = 0;
    const uint64_t nMax = (uint64_t)(unsigned long)-1;
    uint64_t nValue;
    if (!pnErrno) pnErrno = &nLocal;
    nValue = xx_crt_conv_parse(pString, ppEnd, nBase, nMax, &bNegative, &bOverflow, pnErrno);
    if (bOverflow) {
        *pnErrno = XX_CRT_ERANGE;
        return (unsigned long)nMax;
    }
    return bNegative ? (unsigned long)(0 - nValue) : (unsigned long)nValue;
}

long xx_crt_strtol(const char *pString, char **ppEnd, int nBase, int *pnErrno)
{
    int bNegative, bOverflow;
    int nLocal = 0;
    const uint64_t nMaxLong = (uint64_t)((unsigned long)-1 >> 1);
    uint64_t nValue;
    if (!pnErrno) pnErrno = &nLocal;
    nValue = xx_crt_conv_parse(pString, ppEnd, nBase, UINT64_MAX, &bNegative, &bOverflow, pnErrno);
    if (bNegative) {
        if (bOverflow || nValue > nMaxLong + 1u) {
            *pnErrno = XX_CRT_ERANGE;
            return (long)(-(int64_t)nMaxLong - 1);
        }
        /* negate as unsigned: the magnitude may be -LONG_MIN */
        return (long)(int64_t)(0 - nValue);
    }
    if (bOverflow || nValue > nMaxLong) {
        *pnErrno = XX_CRT_ERANGE;
        return (long)nMaxLong;
    }
    return (long)nValue;
}
