/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_bignum.h
 * @brief Fixed-capacity unsigned big integers for the exact float <-> decimal
 *        conversions of the C runtime cores (internal).
 *
 * The capacity covers every intermediate of strtod with 800 significant
 * digits (a 10^1131 divisor shifted against a 2^-1074 scale) and of printf's
 * digit generation. Limbs are 32-bit, little-endian; nCount is trimmed.
 */

#ifndef XX_CRT_BIGNUM_H
#define XX_CRT_BIGNUM_H

#include <stdint.h>
#include <stddef.h>

#define XX_CRT_BIG_LIMBS 136

typedef struct xx_crt_big_s {
    uint32_t pLimb[XX_CRT_BIG_LIMBS];
    int nCount;
} xx_crt_big;

static inline void xx_crt_big_set_u64(xx_crt_big *pBig, uint64_t nValue)
{
    pBig->nCount = 0;
    while (nValue) {
        pBig->pLimb[pBig->nCount++] = (uint32_t)nValue;
        nValue >>= 32;
    }
}

static inline int xx_crt_big_is_zero(const xx_crt_big *pBig)
{
    return pBig->nCount == 0;
}

static inline void xx_crt_big_copy(xx_crt_big *pDestination, const xx_crt_big *pSource)
{
    int i;
    pDestination->nCount = pSource->nCount;
    for (i = 0; i < pSource->nCount; ++i) pDestination->pLimb[i] = pSource->pLimb[i];
}

/* pBig = pBig * nFactor + nAddend; returns 0 when the capacity overflows. */
static inline int xx_crt_big_mul_add(xx_crt_big *pBig, uint32_t nFactor, uint32_t nAddend)
{
    uint64_t nCarry = nAddend;
    int i;
    for (i = 0; i < pBig->nCount; ++i) {
        uint64_t nProduct = (uint64_t)pBig->pLimb[i] * nFactor + nCarry;
        pBig->pLimb[i] = (uint32_t)nProduct;
        nCarry = nProduct >> 32;
    }
    if (nCarry) {
        if (pBig->nCount >= XX_CRT_BIG_LIMBS) return 0;
        pBig->pLimb[pBig->nCount++] = (uint32_t)nCarry;
    }
    return 1;
}

static inline int xx_crt_big_mul_pow10(xx_crt_big *pBig, int nPower)
{
    static const uint32_t s_pow10[10] = {1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u, 1000000000u};
    if (pBig->nCount == 0) return 1;
    while (nPower >= 9) {
        if (!xx_crt_big_mul_add(pBig, 1000000000u, 0)) return 0;
        nPower -= 9;
    }
    if (nPower > 0) return xx_crt_big_mul_add(pBig, s_pow10[nPower], 0);
    return 1;
}

static inline int xx_crt_big_bitlen(const xx_crt_big *pBig)
{
    uint32_t nTop;
    int nBits;
    if (pBig->nCount == 0) return 0;
    nTop = pBig->pLimb[pBig->nCount - 1];
    nBits = (pBig->nCount - 1) * 32;
    while (nTop) {
        ++nBits;
        nTop >>= 1;
    }
    return nBits;
}

static inline int xx_crt_big_shl(xx_crt_big *pBig, int nBits)
{
    int nWords = nBits / 32;
    int nShift = nBits % 32;
    int i;
    if (pBig->nCount == 0 || nBits == 0) return 1;
    if (pBig->nCount + nWords + 1 > XX_CRT_BIG_LIMBS) return 0;
    if (nShift) {
        uint32_t nCarry = 0;
        for (i = 0; i < pBig->nCount; ++i) {
            uint32_t nLimb = pBig->pLimb[i];
            pBig->pLimb[i] = (nLimb << nShift) | nCarry;
            nCarry = nLimb >> (32 - nShift);
        }
        if (nCarry) pBig->pLimb[pBig->nCount++] = nCarry;
    }
    if (nWords) {
        for (i = pBig->nCount - 1; i >= 0; --i) pBig->pLimb[i + nWords] = pBig->pLimb[i];
        for (i = 0; i < nWords; ++i) pBig->pLimb[i] = 0;
        pBig->nCount += nWords;
    }
    return 1;
}

/* Sets pBig to 2^nPower. */
static inline int xx_crt_big_pow2(xx_crt_big *pBig, int nPower)
{
    xx_crt_big_set_u64(pBig, 1);
    return xx_crt_big_shl(pBig, nPower);
}

static inline int xx_crt_big_cmp(const xx_crt_big *pLeft, const xx_crt_big *pRight)
{
    int i;
    if (pLeft->nCount != pRight->nCount) return pLeft->nCount < pRight->nCount ? -1 : 1;
    for (i = pLeft->nCount - 1; i >= 0; --i) {
        if (pLeft->pLimb[i] != pRight->pLimb[i]) return pLeft->pLimb[i] < pRight->pLimb[i] ? -1 : 1;
    }
    return 0;
}

/* pLeft -= pRight; requires pLeft >= pRight. */
static inline void xx_crt_big_sub(xx_crt_big *pLeft, const xx_crt_big *pRight)
{
    uint64_t nBorrow = 0;
    int i;
    for (i = 0; i < pLeft->nCount; ++i) {
        uint64_t nSubtrahend = (uint64_t)(i < pRight->nCount ? pRight->pLimb[i] : 0u) + nBorrow;
        uint64_t nLimb = pLeft->pLimb[i];
        nBorrow = nLimb < nSubtrahend ? 1u : 0u;
        pLeft->pLimb[i] = (uint32_t)(nLimb - nSubtrahend);
    }
    while (pLeft->nCount > 0 && pLeft->pLimb[pLeft->nCount - 1] == 0) --pLeft->nCount;
}

/* Compares pLeft with pRight * nFactor (nFactor <= 10) without building the product. */
static inline int xx_crt_big_cmp_mul(const xx_crt_big *pLeft, const xx_crt_big *pRight, uint32_t nFactor)
{
    xx_crt_big product;
    xx_crt_big_copy(&product, pRight);
    if (!xx_crt_big_mul_add(&product, nFactor, 0)) return -1;
    return xx_crt_big_cmp(pLeft, &product);
}

/* The quotient digit floor(pNum / pDen) for pNum < 10 * pDen; pNum keeps
 * the remainder. */
static inline uint32_t xx_crt_big_div_digit(xx_crt_big *pNum, const xx_crt_big *pDen)
{
    uint32_t nDigit = 0;
    while (xx_crt_big_cmp(pNum, pDen) >= 0) {
        xx_crt_big_sub(pNum, pDen);
        ++nDigit;
    }
    return nDigit;
}

#endif /* XX_CRT_BIGNUM_H */
