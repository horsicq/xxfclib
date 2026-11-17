/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_strtod.c
 * @brief strtod with the syntax, results and errno of the Universal CRT.
 *
 * Syntax: optional white space and sign, then inf / infinity, nan with an
 * optional (n-char-sequence), a hexadecimal significand with an optional
 * binary exponent, or a decimal significand with an optional exponent; the
 * letters are case-insensitive. UCRT's NaN encodings are reproduced: "nan"
 * is 0x7fff...ffff (negated by a minus sign), "nan(ind)" the indeterminate
 * 0xfff8...0, "nan(snan)" the signalling 0x7ff0...01.
 *
 * Results are the correctly rounded (to nearest, ties to even) value of the
 * first 768 significant digits: like UCRT, later digits are ignored, so a
 * string that continues a 768-digit halfway point with nonzero digits still
 * rounds as the tie (Python or Rust would round it up).
 * ERANGE is reported exactly when UCRT reports it: when a nonzero input
 * rounds to zero or a finite input rounds to infinity (not for subnormal
 * results).
 *
 * xx_crt_strtof converts to binary32 directly, as UCRT's strtof and the %f
 * conversion of scanf do: going through a double first would round twice
 * ("1.0000000596046447753906250000000000001" is 0x3F800001, not
 * 0x3F800000). The _n variants read at most a given number of characters.
 */

#include "xxfclib/crt/xx_crt.h"
#include "xx_crt_bignum.h"

#include <stdint.h>

/* UCRT keeps the first 768 significant digits and ignores the rest. */
#define XX_CRT_STRTOD_DIGITS 768

typedef union xx_crt_strtod_bits_u {
    double nDouble;
    uint64_t nBits;
} xx_crt_strtod_bits;

static double xx_crt_from_bits(uint64_t nBits)
{
    xx_crt_strtod_bits u;
    u.nBits = nBits;
    return u.nDouble;
}

static int xx_crt_is_space(char nChar)
{
    return nChar == ' ' || (nChar >= '\t' && nChar <= '\r');
}

static int xx_crt_lower(char nChar)
{
    return (nChar >= 'A' && nChar <= 'Z') ? nChar - 'A' + 'a' : nChar;
}

static int xx_crt_hex_value(char nChar)
{
    if (nChar >= '0' && nChar <= '9') return nChar - '0';
    if (nChar >= 'a' && nChar <= 'f') return nChar - 'a' + 10;
    if (nChar >= 'A' && nChar <= 'F') return nChar - 'A' + 10;
    return -1;
}

/* The target format: binary64 or binary32. */
typedef struct xx_crt_strtod_format_s {
    int nBits;   /* significand bits including the leading one: 53 or 24 */
    int nMinExp; /* exponent of the smallest normal: -1022 or -126 */
    int nMaxExp; /* largest exponent: 1023 or 127 */
} xx_crt_strtod_format;

static const xx_crt_strtod_format s_xx_crt_format_double = {53, -1022, 1023};
static const xx_crt_strtod_format s_xx_crt_format_float = {24, -126, 127};

/* Rounds nTop * 2^nExponent (+ a sticky fraction below the last bit when
 * bSticky; nTop has bit 63 set) to the nearest value of the format, ties to
 * even. Returns the magnitude bits (exponent and fraction fields, no sign). */
static uint64_t xx_crt_round_bits(uint64_t nTop, int bSticky, int nExponent, const xx_crt_strtod_format *pFormat, int *pnErrno)
{
    const uint64_t nInfinity = (uint64_t)(2 * pFormat->nMaxExp + 1) << (pFormat->nBits - 1);
    int nTopExponent = nExponent + 63; /* exponent of the leading bit */
    int nDrop = 64 - pFormat->nBits;
    uint64_t nMantissa;
    uint64_t nRest;
    uint64_t nHalf;

    if (nTopExponent > pFormat->nMaxExp) {
        *pnErrno = XX_CRT_ERANGE;
        return nInfinity;
    }
    if (nTopExponent < pFormat->nMinExp) nDrop += pFormat->nMinExp - nTopExponent;
    if (nDrop > 64) {
        *pnErrno = XX_CRT_ERANGE;
        return 0;
    }
    if (nDrop == 64) {
        nMantissa = 0;
        nRest = nTop;
        nHalf = 0x8000000000000000ull;
    } else {
        nMantissa = nTop >> nDrop;
        nRest = nTop & ((1ull << nDrop) - 1);
        nHalf = 1ull << (nDrop - 1);
    }
    if (nRest > nHalf || (nRest == nHalf && (bSticky || (nMantissa & 1)))) {
        ++nMantissa;
    }
    if (nTopExponent < pFormat->nMinExp) {
        /* subnormal: nMantissa counts units of the smallest subnormal;
         * reaching 2^(nBits-1) makes it the smallest normal, which the same
         * bit pattern encodes. */
        if (nMantissa == 0) *pnErrno = XX_CRT_ERANGE;
        return nMantissa;
    }
    if (nMantissa >> pFormat->nBits) {
        nMantissa >>= 1;
        ++nTopExponent;
        if (nTopExponent > pFormat->nMaxExp) {
            *pnErrno = XX_CRT_ERANGE;
            return nInfinity;
        }
    }
    return ((uint64_t)(nTopExponent + pFormat->nMaxExp) << (pFormat->nBits - 1)) | (nMantissa & ((1ull << (pFormat->nBits - 1)) - 1));
}

/* The leading 64 bits of pBig with bit 63 set, the sticky rest and the
 * exponent of bit 0 of the result. */
static uint64_t xx_crt_big_top64(const xx_crt_big *pBig, int *pbSticky, int *pnExponent)
{
    int nBits = xx_crt_big_bitlen(pBig);
    int nShift = nBits - 64;
    uint64_t nTop = 0;
    int i;
    *pbSticky = 0;
    if (nShift <= 0) {
        for (i = pBig->nCount - 1; i >= 0; --i) nTop = (nTop << 32) | pBig->pLimb[i];
        nTop <<= -nShift;
        *pnExponent = nShift;
        return nTop;
    }
    {
        int nWord = nShift / 32;
        int nBit = nShift % 32;
        /* gather 96 bits starting at word nWord, then shift by nBit */
        uint64_t nLow = pBig->pLimb[nWord];
        uint64_t nMid = nWord + 1 < pBig->nCount ? pBig->pLimb[nWord + 1] : 0;
        uint64_t nHigh = nWord + 2 < pBig->nCount ? pBig->pLimb[nWord + 2] : 0;
        if (nBit == 0) nTop = (nMid << 32) | nLow;
        else nTop = (nHigh << (64 - nBit)) | (nMid << (32 - nBit)) | (nLow >> nBit);
        if (nBit && (nLow & ((1ull << nBit) - 1))) *pbSticky = 1;
        for (i = 0; i < nWord && !*pbSticky; ++i) {
            if (pBig->pLimb[i]) *pbSticky = 1;
        }
    }
    *pnExponent = nShift;
    return nTop;
}

static const double s_xx_crt_pow10[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                          1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

/* UCRT converts the integer and the fractional digits separately. When the
 * fraction has at most nine significant digits (its denominator 10^n fits in
 * 32 bits) and the shifted fraction numerator needs more than 32 bits, its
 * division reports a nonzero remainder even when the division is exact, so
 * an exact halfway value rounds away from zero instead of to even (for
 * example "20037918235722.009765625"). The condition, in UCRT's terms: the
 * integer part I has 1..53 bits, the fraction N/10^n has 1 <= n <= 9 digits
 * (trailing zeros removed) and N * 2^(54 - bits(I)) >= 2^32. Verified
 * against UCRT on 60,000 random halfway values (tests/crt). Returns 1 when
 * an exact tie must round up. */
static int xx_crt_ucrt_tie_rounds_up(const char *pDigits, int nDigits, int nExponent10, int nFormatBits)
{
    uint64_t nInteger = 0;
    uint64_t nFraction = 0;
    int nIntegerDigits;
    int nFractionDigits;
    int nIntegerBits = 0;
    int nFractionBits = 0;
    int i;
    while (nDigits > 0 && pDigits[nDigits - 1] == '0') {
        --nDigits;
        ++nExponent10;
    }
    nIntegerDigits = nDigits + nExponent10;
    nFractionDigits = -nExponent10;
    if (nIntegerDigits <= 0 || nIntegerDigits > 16 || nFractionDigits <= 0 || nFractionDigits > 9) return 0;
    for (i = 0; i < nIntegerDigits; ++i) nInteger = nInteger * 10 + (uint64_t)(pDigits[i] - '0');
    for (; i < nDigits; ++i) nFraction = nFraction * 10 + (uint64_t)(pDigits[i] - '0');
    while (nInteger >> nIntegerBits) ++nIntegerBits;
    while (nFraction >> nFractionBits) ++nFractionBits;
    if (nInteger == 0 || nFraction == 0 || nIntegerBits > nFormatBits) return 0;
    return nFractionBits + (nFormatBits + 1 - nIntegerBits) >= 33;
}

/* D * 10^nExponent10 with D the nDigits decimal digits in pDigits, as the
 * magnitude bits of the format. */
static uint64_t xx_crt_decimal_to_bits(const char *pDigits, int nDigits, int nExponent10, const xx_crt_strtod_format *pFormat, int *pnErrno)
{
    xx_crt_big num;
    int i;
    int bTieUp;

    if (nDigits == 0) return 0;
    bTieUp = xx_crt_ucrt_tie_rounds_up(pDigits, nDigits, nExponent10, pFormat->nBits);
    if (nDigits + nExponent10 > 310) {
        *pnErrno = XX_CRT_ERANGE;
        return (uint64_t)(2 * pFormat->nMaxExp + 1) << (pFormat->nBits - 1);
    }
    if (nDigits + nExponent10 < -323) {
        *pnErrno = XX_CRT_ERANGE;
        return 0;
    }

    /* Exact fast path (binary64 only): an integer below 2^53 times or over
     * an exact power of ten is one correctly rounded IEEE operation. */
    if (pFormat->nBits == 53 && nDigits <= 15 && nExponent10 >= -22 && nExponent10 <= 22 && !bTieUp) {
        uint64_t nValue = 0;
        xx_crt_strtod_bits u;
        for (i = 0; i < nDigits; ++i) nValue = nValue * 10 + (uint64_t)(pDigits[i] - '0');
        u.nDouble = (double)nValue;
        if (nExponent10 >= 0) u.nDouble *= s_xx_crt_pow10[nExponent10];
        else u.nDouble /= s_xx_crt_pow10[-nExponent10];
        return u.nBits;
    }

    num.nCount = 0;
    {
        /* nine digits at a time */
        uint32_t nChunk = 0;
        int nChunkDigits = 0;
        for (i = 0; i < nDigits; ++i) {
            nChunk = nChunk * 10 + (uint32_t)(pDigits[i] - '0');
            if (++nChunkDigits == 9) {
                if (num.nCount == 0) xx_crt_big_set_u64(&num, nChunk);
                else xx_crt_big_mul_add(&num, 1000000000u, nChunk);
                nChunk = 0;
                nChunkDigits = 0;
            }
        }
        if (nChunkDigits) {
            static const uint32_t s_pow[9] = {1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u};
            if (num.nCount == 0) xx_crt_big_set_u64(&num, nChunk);
            else xx_crt_big_mul_add(&num, s_pow[nChunkDigits], nChunk);
        }
    }
    if (num.nCount == 0) return 0;

    if (nExponent10 >= 0) {
        int bSticky;
        int nExponent;
        uint64_t nTop;
        xx_crt_big_mul_pow10(&num, nExponent10);
        nTop = xx_crt_big_top64(&num, &bSticky, &nExponent);
        return xx_crt_round_bits(nTop, bSticky || bTieUp, nExponent, pFormat, pnErrno);
    }
    {
        xx_crt_big den;
        int nShift;
        uint64_t nTop = 0;
        xx_crt_big_set_u64(&den, 1);
        xx_crt_big_mul_pow10(&den, -nExponent10);
        nShift = xx_crt_big_bitlen(&num) - xx_crt_big_bitlen(&den);
        if (nShift > 0) xx_crt_big_shl(&den, nShift);
        else if (nShift < 0) xx_crt_big_shl(&num, -nShift);
        if (xx_crt_big_cmp(&num, &den) < 0) {
            xx_crt_big_shl(&num, 1);
            --nShift;
        }
        /* num/den in [1, 2): value = num/den * 2^nShift */
        for (i = 0; i < 64; ++i) {
            nTop <<= 1;
            if (xx_crt_big_cmp(&num, &den) >= 0) {
                xx_crt_big_sub(&num, &den);
                nTop |= 1;
            }
            xx_crt_big_shl(&num, 1);
        }
        return xx_crt_round_bits(nTop, !xx_crt_big_is_zero(&num) || bTieUp, nShift - 63, pFormat, pnErrno);
    }
}

/* The parser behind strtod and strtof. Reads pString up to its terminator
 * or, when pLimit is not NULL, up to pLimit (exclusive). Returns the bits of
 * the format (sign included). */
#define XX_CRT_AT(q) ((!pLimit || (q) < pLimit) ? *(q) : (char)0)

static uint64_t xx_crt_strto_bits(const char *pString, const char *pLimit, char **ppEnd, int *pnErrno, const xx_crt_strtod_format *pFormat)
{
    const char *p = pString;
    int bNegative = 0;
    int nLocalErrno = 0;
    const uint64_t nSignBit = 1ull << (pFormat->nBits == 53 ? 63 : 31);
    const uint64_t nInfinity = (uint64_t)(2 * pFormat->nMaxExp + 1) << (pFormat->nBits - 1);

    if (!pnErrno) pnErrno = &nLocalErrno;
    while (xx_crt_is_space(XX_CRT_AT(p))) ++p;
    if (XX_CRT_AT(p) == '+' || XX_CRT_AT(p) == '-') {
        bNegative = XX_CRT_AT(p) == '-';
        ++p;
    }

    /* inf / infinity */
    if (xx_crt_lower(XX_CRT_AT(p)) == 'i' && xx_crt_lower(XX_CRT_AT(p + 1)) == 'n' && xx_crt_lower(XX_CRT_AT(p + 2)) == 'f') {
        const char *pInfinity = "inity";
        int i;
        p += 3;
        for (i = 0; i < 5 && xx_crt_lower(XX_CRT_AT(p + i)) == pInfinity[i]; ++i) {
        }
        if (i == 5) p += 5;
        if (ppEnd) *ppEnd = (char *)p;
        return (bNegative ? nSignBit : 0) | nInfinity;
    }
    /* nan, nan(...) */
    if (xx_crt_lower(XX_CRT_AT(p)) == 'n' && xx_crt_lower(XX_CRT_AT(p + 1)) == 'a' && xx_crt_lower(XX_CRT_AT(p + 2)) == 'n') {
        uint64_t nBits = nSignBit - 1; /* "nan": every bit below the sign */
        p += 3;
        if (XX_CRT_AT(p) == '(') {
            const char *pSequence = p + 1;
            const char *q = pSequence;
            while ((XX_CRT_AT(q) >= '0' && XX_CRT_AT(q) <= '9') || (XX_CRT_AT(q) >= 'a' && XX_CRT_AT(q) <= 'z') || (XX_CRT_AT(q) >= 'A' && XX_CRT_AT(q) <= 'Z') || XX_CRT_AT(q) == '_') ++q;
            if (XX_CRT_AT(q) == ')') {
                size_t nLength = (size_t)(q - pSequence);
                if (nLength == 3 && xx_crt_lower(pSequence[0]) == 'i' && xx_crt_lower(pSequence[1]) == 'n' && xx_crt_lower(pSequence[2]) == 'd') {
                    nBits = nSignBit | nInfinity | (1ull << (pFormat->nBits - 2)); /* -nan(ind) */
                    bNegative = 0;
                } else if (nLength == 4 && xx_crt_lower(pSequence[0]) == 's' && xx_crt_lower(pSequence[1]) == 'n' && xx_crt_lower(pSequence[2]) == 'a' &&
                           xx_crt_lower(pSequence[3]) == 'n') {
                    nBits = nInfinity | 1; /* signalling */
                }
                p = q + 1;
            }
        }
        if (ppEnd) *ppEnd = (char *)p;
        return nBits | (bNegative ? nSignBit : 0);
    }

    /* hexadecimal */
    if (XX_CRT_AT(p) == '0' && (XX_CRT_AT(p + 1) == 'x' || XX_CRT_AT(p + 1) == 'X') &&
        (xx_crt_hex_value(XX_CRT_AT(p + 2)) >= 0 || (XX_CRT_AT(p + 2) == '.' && xx_crt_hex_value(XX_CRT_AT(p + 3)) >= 0))) {
        uint64_t nMantissa = 0;
        int nExponent = 0; /* binary exponent of the last kept hex digit */
        int bSticky = 0;
        int bAny = 0;
        int bPoint = 0;
        p += 2;
        for (;; ++p) {
            int nValue = xx_crt_hex_value(XX_CRT_AT(p));
            if (nValue >= 0) {
                if (nMantissa >> 60) {
                    /* beyond 64 bits: only the integer part still scales */
                    if (nValue) bSticky = 1;
                    if (!bPoint) nExponent += 4;
                } else {
                    nMantissa = (nMantissa << 4) | (uint64_t)nValue;
                    if (bPoint) nExponent -= 4;
                }
                bAny = 1;
            } else if (XX_CRT_AT(p) == '.' && !bPoint) {
                bPoint = 1;
            } else {
                break;
            }
        }
        (void)bAny;
        if (XX_CRT_AT(p) == 'p' || XX_CRT_AT(p) == 'P') {
            const char *q = p + 1;
            int bExponentNegative = 0;
            long nValue = 0;
            if (XX_CRT_AT(q) == '+' || XX_CRT_AT(q) == '-') {
                bExponentNegative = XX_CRT_AT(q) == '-';
                ++q;
            }
            if (XX_CRT_AT(q) >= '0' && XX_CRT_AT(q) <= '9') {
                while (XX_CRT_AT(q) >= '0' && XX_CRT_AT(q) <= '9') {
                    if (nValue < 100000) nValue = nValue * 10 + (XX_CRT_AT(q) - '0');
                    ++q;
                }
                nExponent += bExponentNegative ? -(int)nValue : (int)nValue;
                p = q;
            }
        }
        if (ppEnd) *ppEnd = (char *)p;
        if (nMantissa == 0) return bNegative ? nSignBit : 0;
        {
            int nLeading = 0;
            while (!(nMantissa & 0x8000000000000000ull)) {
                nMantissa <<= 1;
                ++nLeading;
            }
            return (bNegative ? nSignBit : 0) | xx_crt_round_bits(nMantissa, bSticky, nExponent - nLeading, pFormat, pnErrno);
        }
    }

    /* decimal */
    {
        char sDigits[XX_CRT_STRTOD_DIGITS + 2];
        int nDigits = 0;
        int nExponent10 = 0;
        int bSticky = 0;
        int bAny = 0;
        int bPoint = 0;
        for (;; ++p) {
            if (XX_CRT_AT(p) >= '0' && XX_CRT_AT(p) <= '9') {
                bAny = 1;
                if (nDigits == 0 && XX_CRT_AT(p) == '0') {
                    if (bPoint) --nExponent10;
                } else if (nDigits < XX_CRT_STRTOD_DIGITS) {
                    sDigits[nDigits++] = XX_CRT_AT(p);
                    if (bPoint) --nExponent10;
                } else {
                    if (XX_CRT_AT(p) != '0') bSticky = 1;
                    if (!bPoint) ++nExponent10;
                }
            } else if (XX_CRT_AT(p) == '.' && !bPoint) {
                bPoint = 1;
            } else {
                break;
            }
        }
        if (!bAny) {
            if (ppEnd) *ppEnd = (char *)pString;
            return 0;
        }
        if (XX_CRT_AT(p) == 'e' || XX_CRT_AT(p) == 'E') {
            const char *q = p + 1;
            int bExponentNegative = 0;
            long nValue = 0;
            if (XX_CRT_AT(q) == '+' || XX_CRT_AT(q) == '-') {
                bExponentNegative = XX_CRT_AT(q) == '-';
                ++q;
            }
            if (XX_CRT_AT(q) >= '0' && XX_CRT_AT(q) <= '9') {
                while (XX_CRT_AT(q) >= '0' && XX_CRT_AT(q) <= '9') {
                    if (nValue < 100000) nValue = nValue * 10 + (XX_CRT_AT(q) - '0');
                    ++q;
                }
                nExponent10 += bExponentNegative ? -(int)nValue : (int)nValue;
                p = q;
            }
        }
        if (ppEnd) *ppEnd = (char *)p;
        (void)bSticky; /* digits past the 768th do not take part, as in UCRT */
        return (bNegative ? nSignBit : 0) | xx_crt_decimal_to_bits(sDigits, nDigits, nExponent10, pFormat, pnErrno);
    }
}

#undef XX_CRT_AT

double xx_crt_strtod(const char *pString, char **ppEnd, int *pnErrno)
{
    return xx_crt_from_bits(xx_crt_strto_bits(pString, NULL, ppEnd, pnErrno, &s_xx_crt_format_double));
}

double xx_crt_strtod_n(const char *pString, size_t nLength, char **ppEnd, int *pnErrno)
{
    return xx_crt_from_bits(xx_crt_strto_bits(pString, pString + nLength, ppEnd, pnErrno, &s_xx_crt_format_double));
}

static float xx_crt_float_from_bits(uint64_t nBits)
{
    union {
        uint32_t nBits;
        float nFloat;
    } u;
    u.nBits = (uint32_t)nBits;
    return u.nFloat;
}

float xx_crt_strtof(const char *pString, char **ppEnd, int *pnErrno)
{
    return xx_crt_float_from_bits(xx_crt_strto_bits(pString, NULL, ppEnd, pnErrno, &s_xx_crt_format_float));
}

float xx_crt_strtof_n(const char *pString, size_t nLength, char **ppEnd, int *pnErrno)
{
    return xx_crt_float_from_bits(xx_crt_strto_bits(pString, pString + nLength, ppEnd, pnErrno, &s_xx_crt_format_float));
}
