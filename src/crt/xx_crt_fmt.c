/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_fmt.c
 * @brief printf formatting with the conversions and spellings of the
 *        Universal CRT.
 *
 * Floating-point digits are exact: the value is carried as a fraction of
 * two big integers and every digit and rounding decision is an integer
 * comparison, so the output is the correctly rounded decimal with exact ties
 * going to the even digit (UCRT's "standard rounding", the default of every
 * MSVC program since VS 2019 16.2). The UCRT-specific spellings are kept:
 * inf, nan, nan(snan), -nan(ind); %p as 16 upper-case digits without a
 * prefix; %a without renormalising a rounded-up leading digit; a %% or an
 * unknown conversion character printed as that character.
 */

#include "xxfclib/crt/xx_crt.h"
#include "xx_crt_bignum.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* --------------------------------------------------------------- output -- */

/* Output goes either straight into a caller's buffer (snprintf: pDirect,
 * nDirectCapacity) or through a small staging buffer to a write callback
 * (streams). nTotal counts every character, stored or not. */
typedef struct xx_crt_out_s {
    xx_crt_write_fn fnWrite;
    void *pContext;
    char *pDirect;
    size_t nDirectCapacity;
    char sBuffer[512];
    size_t nUsed;
    size_t nTotal;
    int bFailed;
} xx_crt_out;

static void xx_crt_out_flush(xx_crt_out *pOut)
{
    if (pOut->pDirect) return;
    if (pOut->nUsed && !pOut->bFailed) {
        if (pOut->fnWrite(pOut->pContext, pOut->sBuffer, pOut->nUsed) != 0) pOut->bFailed = 1;
    }
    pOut->nUsed = 0;
}

static void xx_crt_out_bytes(xx_crt_out *pOut, const char *pData, size_t nSize)
{
    if (pOut->pDirect) {
        /* keep one byte for the terminator the caller writes */
        size_t nRoom = pOut->nTotal + 1 < pOut->nDirectCapacity ? pOut->nDirectCapacity - 1 - pOut->nTotal : 0;
        size_t nCopy = nSize < nRoom ? nSize : nRoom;
        if (nCopy) {
            /* formed only while it points into the buffer */
            char *pTarget = pOut->pDirect + pOut->nTotal;
            size_t i;
            for (i = 0; i < nCopy; ++i) pTarget[i] = pData[i];
        }
        pOut->nTotal += nSize;
        return;
    }
    pOut->nTotal += nSize;
    if (pOut->bFailed) return;
    if (nSize > sizeof(pOut->sBuffer) - pOut->nUsed) {
        xx_crt_out_flush(pOut);
        if (nSize >= sizeof(pOut->sBuffer)) {
            if (!pOut->bFailed && pOut->fnWrite(pOut->pContext, pData, nSize) != 0) pOut->bFailed = 1;
            return;
        }
    }
    {
        size_t i;
        for (i = 0; i < nSize; ++i) pOut->sBuffer[pOut->nUsed + i] = pData[i];
    }
    pOut->nUsed += nSize;
}

static void xx_crt_out_repeat(xx_crt_out *pOut, char nChar, size_t nCount)
{
    char sChunk[64];
    size_t i;
    for (i = 0; i < sizeof(sChunk); ++i) sChunk[i] = nChar;
    while (nCount) {
        size_t nPart = nCount < sizeof(sChunk) ? nCount : sizeof(sChunk);
        xx_crt_out_bytes(pOut, sChunk, nPart);
        nCount -= nPart;
    }
}

/* ------------------------------------------------------- exact digits -- */

typedef union xx_crt_double_bits_u {
    double nDouble;
    uint64_t nBits;
} xx_crt_double_bits;

static uint64_t xx_crt_fmt_bits(double nValue)
{
    xx_crt_double_bits u;
    u.nDouble = nValue;
    return u.nBits;
}

#define XX_CRT_FMT_MAX_DIGITS 800

/* The largest float precision the layout arithmetic accepts: with it every
 * sum of a precision, an exponent and a digit count stays inside int. A
 * larger one cannot produce output that an int return value could count. */
#define XX_CRT_FMT_MAX_PRECISION (2147483647 - 4096)

/* Digits of |nValue| (finite, nonzero). bFixed == 0: nParam significant
 * digits; bFixed != 0: the digits down to 10^-nParam. The result is rounded
 * half-even at the last digit. *pnExp is the decimal exponent of the first
 * digit; *pnCount the number of digits stored (0 when a fixed-point result
 * rounds to zero). *pnZeros receives the count of further zero digits that
 * belong to the result but are not stored (only beyond the exact expansion). */
static void xx_crt_fmt_digits(double nValue, int bFixed, int nParam, char *pDigits, int *pnCount, int *pnExp, int *pnZeros)
{
    xx_crt_big num, den;
    uint64_t nBits = xx_crt_fmt_bits(nValue) & 0x7FFFFFFFFFFFFFFFull;
    uint64_t nMantissa = nBits & 0x000FFFFFFFFFFFFFull;
    int nRawExponent = (int)(nBits >> 52);
    int nExponent;
    int nBitLength = 0;
    int nK;
    int nWanted;
    int nStored;
    int i;
    uint64_t nTemp;

    if (nRawExponent == 0) {
        nExponent = -1074;
    } else {
        nMantissa |= 0x0010000000000000ull;
        nExponent = nRawExponent - 1075;
    }
    nTemp = nMantissa;
    while (nTemp) {
        ++nBitLength;
        nTemp >>= 1;
    }

    if (nExponent >= 0) {
        xx_crt_big_set_u64(&num, nMantissa);
        xx_crt_big_shl(&num, nExponent);
        xx_crt_big_set_u64(&den, 1);
    } else {
        xx_crt_big_set_u64(&num, nMantissa);
        xx_crt_big_pow2(&den, -nExponent);
    }

    /* 0.30102999566398120 = log10(2); the estimate is corrected below. */
    {
        double nEstimate = (double)(nExponent + nBitLength - 1) * 0.30102999566398120;
        nK = (int)nEstimate;
        if ((double)nK > nEstimate) --nK;
    }
    if (nK > 0) {
        xx_crt_big_mul_pow10(&den, nK);
    } else if (nK < 0) {
        xx_crt_big_mul_pow10(&num, -nK);
    }
    while (xx_crt_big_cmp(&num, &den) < 0) {
        xx_crt_big_mul_add(&num, 10, 0);
        --nK;
    }
    while (xx_crt_big_cmp_mul(&num, &den, 10) >= 0) {
        xx_crt_big_mul_add(&den, 10, 0);
        ++nK;
    }

    *pnZeros = 0;
    nWanted = bFixed ? (nK + 1 + nParam) : nParam;
    if (nWanted < 0) {
        *pnCount = 0;
        *pnExp = -nParam;
        return;
    }
    if (nWanted == 0) {
        /* The first digit lies just past the last kept position; the kept
         * digit before it is an implicit 0, so an exact tie stays down. */
        if (xx_crt_big_cmp_mul(&num, &den, 5) > 0) {
            pDigits[0] = '1';
            *pnCount = 1;
            *pnExp = nK + 1;
        } else {
            *pnCount = 0;
            *pnExp = -nParam;
        }
        return;
    }

    nStored = nWanted < XX_CRT_FMT_MAX_DIGITS ? nWanted : XX_CRT_FMT_MAX_DIGITS;
    for (i = 0; i < nStored; ++i) {
        if (xx_crt_big_is_zero(&num)) break;
        pDigits[i] = (char)('0' + xx_crt_big_div_digit(&num, &den));
        xx_crt_big_mul_add(&num, 10, 0);
    }
    if (i < nStored) {
        /* The expansion ended: the remaining digits are zeros, no rounding. */
        int j;
        for (j = i; j < nStored; ++j) pDigits[j] = '0';
        *pnCount = nStored;
        *pnExp = nK;
        *pnZeros = nWanted - nStored;
        return;
    }
    *pnZeros = nWanted - nStored;
    {
        int nCompare = xx_crt_big_cmp_mul(&num, &den, 5);
        /* Digits past the stored ones are zeros (no expansion is longer than
         * 767 significant digits), so the remainder alone decides. */
        int bUp = nCompare > 0 || (nCompare == 0 && ((pDigits[nStored - 1] - '0') & 1));
        if (bUp) {
            int j = nStored - 1;
            while (j >= 0 && pDigits[j] == '9') {
                pDigits[j] = '0';
                --j;
            }
            if (j >= 0) {
                pDigits[j]++;
            } else {
                /* 99..9 -> 100..0: one more leading digit. */
                for (j = nStored; j > 0; --j) pDigits[j] = pDigits[j - 1];
                pDigits[0] = '1';
                ++nK;
                if (bFixed) {
                    ++nStored;
                }
            }
        }
    }
    *pnCount = nStored;
    *pnExp = nK;
}

/* ------------------------------------------------- one float conversion -- */

/* A conversion result as a few pieces, so a large precision never needs a
 * large buffer: text, then nZeros '0', then more text, then nZeros2 '0',
 * then the suffix. */
typedef struct xx_crt_fmt_float_s {
    char sSign[2];
    int nSign;
    char sPrefix[2];
    int nPrefix;
    char sText[XX_CRT_FMT_MAX_DIGITS + 340];
    int nText;
    int nZeros;
    char sText2[XX_CRT_FMT_MAX_DIGITS + 8];
    int nText2;
    int nZeros2;
    char sSuffix[16];
    int nSuffix;
    int bSpecial;
} xx_crt_fmt_float;

static int xx_crt_fmt_exponent(char *pOut, char nLetter, int nExponent, int nMinDigits)
{
    char sDigits[8];
    int nDigits = 0;
    int nLength = 0;
    unsigned nMagnitude = (unsigned)(nExponent < 0 ? -nExponent : nExponent);
    pOut[nLength++] = nLetter;
    pOut[nLength++] = nExponent < 0 ? '-' : '+';
    do {
        sDigits[nDigits++] = (char)('0' + nMagnitude % 10);
        nMagnitude /= 10;
    } while (nMagnitude);
    while (nDigits < nMinDigits) sDigits[nDigits++] = '0';
    while (nDigits) pOut[nLength++] = sDigits[--nDigits];
    return nLength;
}

/* Fixed notation of nCount digits starting at decimal exponent nExp, with
 * nZeros more zero digits, exactly nFraction fraction digits. */
static void xx_crt_fmt_layout_fixed(xx_crt_fmt_float *pResult, const char *pDigits, int nCount, int nExp, int nFraction, int bPoint)
{
    int nIntegerDigits = nExp + 1;
    int i;
    if (nCount == 0) {
        nIntegerDigits = 0;
    }
    if (nIntegerDigits <= 0) {
        pResult->sText[pResult->nText++] = '0';
        if (bPoint) pResult->sText[pResult->nText++] = '.';
        {
            /* -nExp - 1 leading fraction zeros, then the digits */
            int nLead = nCount ? (-nExp - 1) : nFraction;
            if (nLead > nFraction) nLead = nFraction;
            pResult->nZeros = nLead;
            {
                int nTake = nCount;
                if (nTake > nFraction - nLead) nTake = nFraction - nLead;
                for (i = 0; i < nTake; ++i) pResult->sText2[pResult->nText2++] = pDigits[i];
                pResult->nZeros2 = nFraction - nLead - nTake;
            }
        }
        return;
    }
    /* integer part */
    for (i = 0; i < nIntegerDigits && i < nCount; ++i) pResult->sText[pResult->nText++] = pDigits[i];
    if (nCount < nIntegerDigits) {
        /* integer digits beyond the stored ones are zeros, then the point */
        pResult->nZeros = nIntegerDigits - nCount;
        if (bPoint) pResult->sText2[pResult->nText2++] = '.';
        pResult->nZeros2 = nFraction;
        return;
    }
    if (bPoint) pResult->sText[pResult->nText++] = '.';
    {
        int nTake = nCount - nIntegerDigits;
        if (nTake > nFraction) nTake = nFraction;
        for (i = 0; i < nTake; ++i) pResult->sText[pResult->nText++] = pDigits[nIntegerDigits + i];
        pResult->nZeros = nFraction - nTake;
    }
}

static void xx_crt_fmt_special(xx_crt_fmt_float *pResult, uint64_t nBits, int bUpper)
{
    const char *pText;
    pResult->bSpecial = 1;
    if ((nBits & 0x000FFFFFFFFFFFFFull) == 0) {
        pText = bUpper ? "INF" : "inf";
    } else if ((nBits & 0x0008000000000000ull) == 0) {
        pText = bUpper ? "NAN(SNAN)" : "nan(snan)";
    } else if ((nBits >> 63) && (nBits & 0x0007FFFFFFFFFFFFull) == 0) {
        pText = bUpper ? "NAN(IND)" : "nan(ind)";
    } else {
        pText = bUpper ? "NAN" : "nan";
    }
    while (*pText) pResult->sText[pResult->nText++] = *pText++;
}

static void xx_crt_fmt_hex(xx_crt_fmt_float *pResult, uint64_t nBits, int nPrecision, int bAlt, int bUpper)
{
    const char *pHex = bUpper ? "0123456789ABCDEF" : "0123456789abcdef";
    uint64_t nFraction = nBits & 0x000FFFFFFFFFFFFFull;
    int nRaw = (int)((nBits >> 52) & 0x7FF);
    int nExponent;
    unsigned nLead;
    int i;
    pResult->sPrefix[0] = '0';
    pResult->sPrefix[1] = bUpper ? 'X' : 'x';
    pResult->nPrefix = 2;
    if (nRaw == 0 && nFraction == 0) {
        nLead = 0;
        nExponent = 0;
    } else if (nRaw == 0) {
        nLead = 0;
        nExponent = -1022;
    } else {
        nLead = 1;
        nExponent = nRaw - 1023;
    }
    if (nPrecision < 0) nPrecision = 13;
    if (nPrecision < 13) {
        int nDrop = 52 - 4 * nPrecision;
        uint64_t nRest = nFraction & ((1ull << nDrop) - 1);
        uint64_t nHalf = 1ull << (nDrop - 1);
        uint64_t nKept = nFraction >> nDrop;
        /* half-even on the last kept hex digit, which is the leading digit
         * itself at precision 0 */
        if (nRest > nHalf || (nRest == nHalf && ((nPrecision == 0 ? nLead : (unsigned)nKept) & 1))) {
            ++nKept;
            if (nPrecision == 0 || (nKept >> (4 * nPrecision))) {
                nLead += 1;
                nKept &= nPrecision ? ((1ull << (4 * nPrecision)) - 1) : 0;
            }
        }
        nFraction = nKept << nDrop;
    }
    pResult->sText[pResult->nText++] = pHex[nLead];
    if (nPrecision > 0 || bAlt) pResult->sText[pResult->nText++] = '.';
    for (i = 0; i < nPrecision && i < 13; ++i) pResult->sText[pResult->nText++] = pHex[(nFraction >> (48 - 4 * i)) & 0xF];
    if (nPrecision > 13) pResult->nZeros = nPrecision - 13;
    pResult->nSuffix = xx_crt_fmt_exponent(pResult->sSuffix, bUpper ? 'P' : 'p', nExponent, 1);
}

static void xx_crt_fmt_float_convert(xx_crt_fmt_float *pResult, double nValue, char nConversion, int nPrecision, int bAlt, int bPlus, int bSpace)
{
    uint64_t nBits = xx_crt_fmt_bits(nValue);
    int bUpper = (nConversion >= 'A' && nConversion <= 'Z');
    char nLower = (char)(bUpper ? nConversion - 'A' + 'a' : nConversion);
    char sDigits[XX_CRT_FMT_MAX_DIGITS + 2];
    int nCount = 0, nExp = 0, nZeros = 0;

    pResult->nSign = 0;
    pResult->nPrefix = 0;
    pResult->nText = 0;
    pResult->nZeros = 0;
    pResult->nText2 = 0;
    pResult->nZeros2 = 0;
    pResult->nSuffix = 0;
    pResult->bSpecial = 0;

    if (nBits >> 63) {
        pResult->sSign[pResult->nSign++] = '-';
    } else if (bPlus) {
        pResult->sSign[pResult->nSign++] = '+';
    } else if (bSpace) {
        pResult->sSign[pResult->nSign++] = ' ';
    }

    if (((nBits >> 52) & 0x7FF) == 0x7FF) {
        xx_crt_fmt_special(pResult, nBits, bUpper);
        if (bAlt && nPrecision == 0 && nLower != 'g') {
            /* UCRT's '#' with precision 0 forces a decimal point after the
             * first character of the sign-and-text: "i.nf", "n.an", but
             * "-.inf"; %a also gives a negative value its 0x ("-0x.inf"),
             * and the negative forms take zero padding like a number. */
            int i;
            if (pResult->nSign && pResult->sSign[0] == '-') {
                for (i = pResult->nText; i > 0; --i) pResult->sText[i] = pResult->sText[i - 1];
                pResult->sText[0] = '.';
                ++pResult->nText;
                pResult->bSpecial = 0;
                if (nLower == 'a') {
                    pResult->sPrefix[0] = '0';
                    pResult->sPrefix[1] = bUpper ? 'X' : 'x';
                    pResult->nPrefix = 2;
                }
            } else {
                for (i = pResult->nText; i > 1; --i) pResult->sText[i] = pResult->sText[i - 1];
                pResult->sText[1] = '.';
                ++pResult->nText;
            }
        }
        return;
    }
    if (nLower == 'a') {
        xx_crt_fmt_hex(pResult, nBits, nPrecision, bAlt, bUpper);
        return;
    }
    if (nPrecision < 0) nPrecision = 6;

    if (nLower == 'f') {
        if ((nBits << 1) == 0) {
            nCount = 0;
            nExp = 0;
        } else {
            xx_crt_fmt_digits(nValue, 1, nPrecision, sDigits, &nCount, &nExp, &nZeros);
        }
        xx_crt_fmt_layout_fixed(pResult, sDigits, nCount, nExp, nPrecision, nPrecision > 0 || bAlt);
        return;
    }

    if (nLower == 'e') {
        int nSignificant = nPrecision + 1;
        if ((nBits << 1) == 0) {
            nCount = 0;
            nExp = 0;
        } else {
            xx_crt_fmt_digits(nValue, 0, nSignificant > XX_CRT_FMT_MAX_DIGITS ? XX_CRT_FMT_MAX_DIGITS : nSignificant, sDigits, &nCount, &nExp, &nZeros);
        }
        pResult->sText[pResult->nText++] = nCount ? sDigits[0] : '0';
        if (nPrecision > 0 || bAlt) pResult->sText[pResult->nText++] = '.';
        {
            int nTake = nCount > 0 ? nCount - 1 : 0;
            int i;
            if (nTake > nPrecision) nTake = nPrecision;
            for (i = 0; i < nTake; ++i) pResult->sText[pResult->nText++] = sDigits[1 + i];
            pResult->nZeros = nPrecision - nTake;
        }
        pResult->nSuffix = xx_crt_fmt_exponent(pResult->sSuffix, bUpper ? 'E' : 'e', nCount ? nExp : 0, 2);
        return;
    }

    /* %g */
    {
        int nSignificant = nPrecision == 0 ? 1 : nPrecision;
        int nX;
        int nStoredSig = nSignificant > XX_CRT_FMT_MAX_DIGITS ? XX_CRT_FMT_MAX_DIGITS : nSignificant;
        if ((nBits << 1) == 0) {
            int i;
            for (i = 0; i < nStoredSig; ++i) sDigits[i] = '0';
            nCount = nStoredSig;
            nExp = 0;
        } else {
            xx_crt_fmt_digits(nValue, 0, nStoredSig, sDigits, &nCount, &nExp, &nZeros);
        }
        nX = nExp;
        if (nX >= -4 && nX < nSignificant) {
            int nFraction = nSignificant - 1 - nX;
            int nLength = nCount;
            if (!bAlt) {
                /* strip trailing zeros of the significant digits that fall in the fraction */
                while (nLength > 0 && nLength > nX + 1 && sDigits[nLength - 1] == '0') --nLength;
                if (nLength - (nX + 1) < nFraction) nFraction = nLength - (nX + 1) > 0 ? nLength - (nX + 1) : 0;
                if ((nBits << 1) == 0) nFraction = 0;
            }
            if ((nBits << 1) == 0) {
                pResult->sText[pResult->nText++] = '0';
                if (nFraction > 0 || bAlt) pResult->sText[pResult->nText++] = '.';
                pResult->nZeros = nFraction;
                return;
            }
            xx_crt_fmt_layout_fixed(pResult, sDigits, nLength, nX, nFraction, nFraction > 0 || bAlt);
        } else {
            int nLength = nCount;
            int i;
            if (!bAlt) {
                while (nLength > 1 && sDigits[nLength - 1] == '0') --nLength;
            }
            pResult->sText[pResult->nText++] = sDigits[0];
            if (nLength > 1 || bAlt) pResult->sText[pResult->nText++] = '.';
            for (i = 1; i < nLength; ++i) pResult->sText[pResult->nText++] = sDigits[i];
            if (bAlt) pResult->nZeros = nSignificant - nCount;
            pResult->nSuffix = xx_crt_fmt_exponent(pResult->sSuffix, bUpper ? 'E' : 'e', nX, 2);
        }
    }
}

static size_t xx_crt_fmt_float_length(const xx_crt_fmt_float *pResult)
{
    return (size_t)pResult->nSign + (size_t)pResult->nPrefix + (size_t)pResult->nText + (size_t)pResult->nZeros + (size_t)pResult->nText2 +
           (size_t)pResult->nZeros2 + (size_t)pResult->nSuffix;
}

static void xx_crt_fmt_float_emit(xx_crt_out *pOut, const xx_crt_fmt_float *pResult, int nWidth, int bLeft, int bZero)
{
    size_t nLength = xx_crt_fmt_float_length(pResult);
    size_t nPad = (nWidth > 0 && (size_t)nWidth > nLength) ? (size_t)nWidth - nLength : 0;
    if (pResult->bSpecial) bZero = 0;
    if (!bLeft && !bZero) xx_crt_out_repeat(pOut, ' ', nPad);
    xx_crt_out_bytes(pOut, pResult->sSign, (size_t)pResult->nSign);
    xx_crt_out_bytes(pOut, pResult->sPrefix, (size_t)pResult->nPrefix);
    if (!bLeft && bZero) xx_crt_out_repeat(pOut, '0', nPad);
    xx_crt_out_bytes(pOut, pResult->sText, (size_t)pResult->nText);
    xx_crt_out_repeat(pOut, '0', (size_t)pResult->nZeros);
    xx_crt_out_bytes(pOut, pResult->sText2, (size_t)pResult->nText2);
    xx_crt_out_repeat(pOut, '0', (size_t)pResult->nZeros2);
    xx_crt_out_bytes(pOut, pResult->sSuffix, (size_t)pResult->nSuffix);
    if (bLeft) xx_crt_out_repeat(pOut, ' ', nPad);
}

int xx_crt_format_double(double nValue, char nConversion, int nPrecision, int bAlt, int bPlus, int bSpace, char *pBuffer, size_t nBufferSize)
{
    static const char s_dummy = 0;
    xx_crt_fmt_float result;
    xx_crt_out out;
    (void)s_dummy;
    out.fnWrite = NULL;
    out.pContext = NULL;
    out.pDirect = pBuffer;
    out.nDirectCapacity = pBuffer ? nBufferSize : 0;
    out.nUsed = 0;
    out.nTotal = 0;
    out.bFailed = 0;
    if (!pBuffer) {
        /* counting only: a target that never receives data */
        static char s_scratch[1];
        out.pDirect = s_scratch;
    }
    if (nPrecision > XX_CRT_FMT_MAX_PRECISION) return -1;
    xx_crt_fmt_float_convert(&result, nValue, nConversion, nPrecision, bAlt, bPlus, bSpace);
    xx_crt_fmt_float_emit(&out, &result, 0, 0, 0);
    if (pBuffer && nBufferSize) pBuffer[out.nTotal < nBufferSize ? out.nTotal : nBufferSize - 1] = 0;
    return (int)out.nTotal;
}

/* ------------------------------------------------------------- integers -- */

static void xx_crt_fmt_integer(xx_crt_out *pOut, uint64_t nMagnitude, int bNegative, unsigned nBase, int bUpper, int bAlt, int bPlus, int bSpace,
                               int nWidth, int nPrecision, int bLeft, int bZero, int bPointer)
{
    const char *pDigitSet = bUpper ? "0123456789ABCDEF" : "0123456789abcdef";
    char sDigits[72];
    char sPrefix[3];
    int nDigits = 0;
    int nPrefix = 0;
    size_t nZeros = 0;
    size_t nLength;
    size_t nPad;
    uint64_t nRest = nMagnitude;

    while (nRest) {
        sDigits[sizeof(sDigits) - 1 - nDigits] = pDigitSet[nRest % nBase];
        nRest /= nBase;
        ++nDigits;
    }
    if (bNegative) {
        sPrefix[nPrefix++] = '-';
    } else if (bPlus) {
        sPrefix[nPrefix++] = '+';
    } else if (bSpace) {
        sPrefix[nPrefix++] = ' ';
    }
    if (bAlt && nBase == 16 && nMagnitude != 0 && !bPointer) {
        sPrefix[nPrefix++] = '0';
        sPrefix[nPrefix++] = bUpper ? 'X' : 'x';
    }
    if (nPrecision >= 0) {
        bZero = 0;
        if (nPrecision > nDigits) nZeros = (size_t)(nPrecision - nDigits);
    } else if (nDigits == 0) {
        nZeros = 1;
    }
    if (bAlt && nBase == 8 && nZeros == 0 && (nDigits == 0 || sDigits[sizeof(sDigits) - nDigits] != '0')) nZeros = 1;
    nLength = (size_t)nPrefix + nZeros + (size_t)nDigits;
    nPad = (nWidth > 0 && (size_t)nWidth > nLength) ? (size_t)nWidth - nLength : 0;
    if (!bLeft && !bZero) xx_crt_out_repeat(pOut, ' ', nPad);
    xx_crt_out_bytes(pOut, sPrefix, (size_t)nPrefix);
    if (!bLeft && bZero) xx_crt_out_repeat(pOut, '0', nPad);
    xx_crt_out_repeat(pOut, '0', nZeros);
    xx_crt_out_bytes(pOut, sDigits + sizeof(sDigits) - nDigits, (size_t)nDigits);
    if (bLeft) xx_crt_out_repeat(pOut, ' ', nPad);
}

/* %s and %c: UCRT pads with zeros when the 0 flag is given without -. */
static void xx_crt_fmt_padded(xx_crt_out *pOut, const char *pText, size_t nLength, int nWidth, int bLeft, int bZero)
{
    size_t nPad = (nWidth > 0 && (size_t)nWidth > nLength) ? (size_t)nWidth - nLength : 0;
    if (!bLeft) xx_crt_out_repeat(pOut, bZero ? '0' : ' ', nPad);
    xx_crt_out_bytes(pOut, pText, nLength);
    if (bLeft) xx_crt_out_repeat(pOut, ' ', nPad);
}

/* ---------------------------------------------------------------- engine -- */

enum {
    XX_CRT_LEN_DEFAULT,
    XX_CRT_LEN_HH,
    XX_CRT_LEN_H,
    XX_CRT_LEN_L,
    XX_CRT_LEN_LL,
    XX_CRT_LEN_64,
    XX_CRT_LEN_32,
    XX_CRT_LEN_PTR,
    XX_CRT_LEN_LONGDOUBLE,
    XX_CRT_LEN_WIDE
};

static int xx_crt_vformat_out(xx_crt_out *pOut, const char *pFormat, va_list args)
{
#define out (*pOut)
    const char *p = pFormat;
    int bConversionError = 0;

    while (*p) {
        int bLeft = 0, bPlus = 0, bSpace = 0, bAlt = 0, bZero = 0;
        int nWidth = 0;
        int nPrecision = -1;
        int nLength = XX_CRT_LEN_DEFAULT;
        char nConversion;

        if (*p != '%') {
            const char *pRun = p;
            while (*p && *p != '%') ++p;
            xx_crt_out_bytes(&out, pRun, (size_t)(p - pRun));
            continue;
        }
        ++p;
        for (;;) {
            if (*p == '-') bLeft = 1;
            else if (*p == '+') bPlus = 1;
            else if (*p == ' ') bSpace = 1;
            else if (*p == '#') bAlt = 1;
            else if (*p == '0') bZero = 1;
            else break;
            ++p;
        }
        if (*p == '*') {
            nWidth = va_arg(args, int);
            if (nWidth < 0) {
                bLeft = 1;
                nWidth = nWidth == (-2147483647 - 1) ? 2147483647 : -nWidth;
            }
            ++p;
        } else {
            while (*p >= '0' && *p <= '9') {
                /* saturates: no signed overflow on an absurd literal */
                nWidth = nWidth > (2147483647 - (*p - '0')) / 10 ? 2147483647 : nWidth * 10 + (*p - '0');
                ++p;
            }
        }
        if (*p == '.') {
            ++p;
            nPrecision = 0;
            if (*p == '*') {
                nPrecision = va_arg(args, int);
                if (nPrecision < 0) nPrecision = -1;
                ++p;
            } else {
                while (*p >= '0' && *p <= '9') {
                    nPrecision = nPrecision > (2147483647 - (*p - '0')) / 10 ? 2147483647 : nPrecision * 10 + (*p - '0');
                    ++p;
                }
            }
        }
        /* length modifiers */
        if (p[0] == 'h') {
            if (p[1] == 'h') {
                nLength = XX_CRT_LEN_HH;
                p += 2;
            } else {
                nLength = XX_CRT_LEN_H;
                ++p;
            }
        } else if (p[0] == 'l') {
            if (p[1] == 'l') {
                nLength = XX_CRT_LEN_LL;
                p += 2;
            } else {
                nLength = XX_CRT_LEN_L;
                ++p;
            }
        } else if (p[0] == 'L') {
            nLength = XX_CRT_LEN_LONGDOUBLE;
            ++p;
        } else if (p[0] == 'j' || p[0] == 'z' || p[0] == 't') {
            nLength = sizeof(size_t) == 8 ? XX_CRT_LEN_64 : XX_CRT_LEN_32;
            ++p;
        } else if (p[0] == 'I') {
            if (p[1] == '6' && p[2] == '4') {
                nLength = XX_CRT_LEN_64;
                p += 3;
            } else if (p[1] == '3' && p[2] == '2') {
                nLength = XX_CRT_LEN_32;
                p += 3;
            } else {
                nLength = XX_CRT_LEN_PTR;
                ++p;
            }
        } else if (p[0] == 'w') {
            nLength = XX_CRT_LEN_WIDE;
            ++p;
        }
        nConversion = *p;
        if (nConversion == 0) break;
        ++p;

        switch (nConversion) {
            case 'd':
            case 'i': {
                int64_t nValue;
                if (nLength == XX_CRT_LEN_LL || nLength == XX_CRT_LEN_64 || (nLength == XX_CRT_LEN_PTR && sizeof(void *) == 8)) {
                    nValue = va_arg(args, long long);
                } else if (nLength == XX_CRT_LEN_HH) {
                    nValue = (signed char)va_arg(args, int);
                } else if (nLength == XX_CRT_LEN_H) {
                    nValue = (short)va_arg(args, int);
                } else if (nLength == XX_CRT_LEN_L) {
                    nValue = va_arg(args, long);
                } else {
                    nValue = va_arg(args, int);
                }
                xx_crt_fmt_integer(&out, nValue < 0 ? (uint64_t)0 - (uint64_t)nValue : (uint64_t)nValue, nValue < 0, 10, 0, bAlt, bPlus, bSpace, nWidth,
                                   nPrecision, bLeft, bZero, 0);
                break;
            }
            case 'u':
            case 'o':
            case 'x':
            case 'X': {
                uint64_t nValue;
                unsigned nBase = nConversion == 'u' ? 10u : nConversion == 'o' ? 8u : 16u;
                if (nLength == XX_CRT_LEN_LL || nLength == XX_CRT_LEN_64 || (nLength == XX_CRT_LEN_PTR && sizeof(void *) == 8)) {
                    nValue = va_arg(args, unsigned long long);
                } else if (nLength == XX_CRT_LEN_HH) {
                    nValue = (unsigned char)va_arg(args, unsigned int);
                } else if (nLength == XX_CRT_LEN_H) {
                    nValue = (unsigned short)va_arg(args, unsigned int);
                } else if (nLength == XX_CRT_LEN_L) {
                    nValue = va_arg(args, unsigned long);
                } else {
                    nValue = va_arg(args, unsigned int);
                }
                xx_crt_fmt_integer(&out, nValue, 0, nBase, nConversion == 'X', bAlt, 0, 0, nWidth, nPrecision, bLeft, bZero, 0);
                break;
            }
            case 'p': {
                uint64_t nValue = (uint64_t)(uintptr_t)va_arg(args, void *);
                xx_crt_fmt_integer(&out, nValue, 0, 16, 1, 0, 0, 0, nWidth, (int)(2 * sizeof(void *)), bLeft, 0, 1);
                break;
            }
            case 'c':
            case 'C': {
                int bWide = (nConversion == 'C') ? (nLength != XX_CRT_LEN_H) : (nLength == XX_CRT_LEN_L || nLength == XX_CRT_LEN_WIDE);
                char nChar;
                if (bWide) {
                    unsigned nWide = (unsigned)(unsigned short)va_arg(args, int);
                    if (nWide > 0xFF) {
                        bConversionError = 1;
                        break;
                    }
                    nChar = (char)nWide;
                } else {
                    nChar = (char)va_arg(args, int);
                }
                xx_crt_fmt_padded(&out, &nChar, 1, nWidth, bLeft, bZero);
                break;
            }
            case 's':
            case 'S': {
                int bWide = (nConversion == 'S') ? (nLength != XX_CRT_LEN_H) : (nLength == XX_CRT_LEN_L || nLength == XX_CRT_LEN_WIDE);
                if (bWide) {
                    const wchar_t *pWide = va_arg(args, const wchar_t *);
                    char sChunk[256];
                    size_t nChars = 0;
                    size_t nPad;
                    size_t i;
                    if (pWide == NULL) pWide = L"(null)";
                    while ((nPrecision < 0 || nChars < (size_t)nPrecision) && pWide[nChars]) {
                        if ((unsigned)pWide[nChars] > 0xFF) {
                            bConversionError = 1;
                            break;
                        }
                        ++nChars;
                    }
                    if (bConversionError) break;
                    nPad = (nWidth > 0 && (size_t)nWidth > nChars) ? (size_t)nWidth - nChars : 0;
                    if (!bLeft) xx_crt_out_repeat(&out, bZero ? '0' : ' ', nPad);
                    for (i = 0; i < nChars;) {
                        size_t nPart = 0;
                        while (nPart < sizeof(sChunk) && i < nChars) sChunk[nPart++] = (char)pWide[i++];
                        xx_crt_out_bytes(&out, sChunk, nPart);
                    }
                    if (bLeft) xx_crt_out_repeat(&out, ' ', nPad);
                } else {
                    const char *pText = va_arg(args, const char *);
                    size_t nChars = 0;
                    if (pText == NULL) pText = "(null)";
                    if (nPrecision < 0) {
                        while (pText[nChars]) ++nChars;
                    } else {
                        while (nChars < (size_t)nPrecision && pText[nChars]) ++nChars;
                    }
                    xx_crt_fmt_padded(&out, pText, nChars, nWidth, bLeft, bZero);
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
                double nValue = va_arg(args, double);
                xx_crt_fmt_float result;
                if (nPrecision > XX_CRT_FMT_MAX_PRECISION) {
                    /* more digits than an int can count: the call fails, and
                     * the layout arithmetic below never sees the value */
                    bConversionError = 1;
                    break;
                }
                xx_crt_fmt_float_convert(&result, nValue, nConversion, nPrecision, bAlt, bPlus, bSpace);
                xx_crt_fmt_float_emit(&out, &result, nWidth, bLeft, bZero);
                break;
            }
            case 'n': {
                /* UCRT rejects %n unless _set_printf_count_output was called; the
                 * argument is consumed and nothing is stored. */
                (void)va_arg(args, void *);
                break;
            }
            default:
                /* %% and unknown conversion characters print the character. */
                xx_crt_out_bytes(&out, &nConversion, 1);
                break;
        }
        if (bConversionError) break;
    }
    xx_crt_out_flush(&out);
    if (bConversionError || out.bFailed) return -1;
    return out.nTotal > 0x7FFFFFFFu ? -1 : (int)out.nTotal;
#undef out
}

int xx_crt_vformat(xx_crt_write_fn fnWrite, void *pContext, const char *pFormat, va_list args)
{
    xx_crt_out out;
    out.fnWrite = fnWrite;
    out.pContext = pContext;
    out.pDirect = NULL;
    out.nDirectCapacity = 0;
    out.nUsed = 0;
    out.nTotal = 0;
    out.bFailed = 0;
    return xx_crt_vformat_out(&out, pFormat, args);
}

/* A direct buffer sink: snprintf is the hot caller, so it skips the
 * intermediate buffer of the generic sink by writing in place. */
int xx_crt_vsnprintf(char *pBuffer, size_t nSize, const char *pFormat, va_list args)
{
    xx_crt_out out;
    int nResult;
    out.fnWrite = NULL;
    out.pContext = NULL;
    out.pDirect = pBuffer;
    out.nDirectCapacity = pBuffer ? nSize : 0;
    out.nUsed = 0;
    out.nTotal = 0;
    out.bFailed = 0;
    if (!pBuffer) {
        /* counting only: a one-byte scratch target that never receives data */
        static char s_scratch[1];
        out.pDirect = s_scratch;
        out.nDirectCapacity = 0;
    }
    nResult = xx_crt_vformat_out(&out, pFormat, args);
    if (pBuffer && nSize) pBuffer[out.nTotal < nSize ? out.nTotal : nSize - 1] = 0;
    return nResult;
}

/* The legacy _vsnprintf shape: stores at most nCount characters and never a
 * terminator; returns the length of the complete output. */
int xx_crt_vsnprintf_raw(char *pBuffer, size_t nCount, const char *pFormat, va_list args)
{
    xx_crt_out out;
    out.fnWrite = NULL;
    out.pContext = NULL;
    out.pDirect = pBuffer;
    /* the direct sink keeps one byte back for a terminator */
    out.nDirectCapacity = nCount == (size_t)-1 ? nCount : nCount + 1;
    out.nUsed = 0;
    out.nTotal = 0;
    out.bFailed = 0;
    return xx_crt_vformat_out(&out, pFormat, args);
}

int xx_crt_snprintf(char *pBuffer, size_t nSize, const char *pFormat, ...)
{
    va_list args;
    int nResult;
    va_start(args, pFormat);
    nResult = xx_crt_vsnprintf(pBuffer, nSize, pFormat, args);
    va_end(args);
    return nResult;
}
