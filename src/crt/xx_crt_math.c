/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_math.c
 * @brief The <math.h> functions of the standard-name C runtime layer.
 *
 * floor, ceil, trunc, fabs and sqrt are exact and reproduce the Universal
 * CRT bit for bit, including signed zeros and NaN handling (a signalling
 * NaN comes back quiet, a domain error is the "indeterminate" -nan(ind)).
 *
 * log2 is correctly rounded. A fast phase evaluates it in double-double
 * arithmetic (Dekker products, no FMA needed) with a 512-entry table built
 * once on first use, with an error below 2^-69 of the result; when a 2^-66
 * bound does not decide the rounding, an accurate atanh series of about
 * 2^-100 decides it. UCRT's own log2 is not correctly rounded: it differs
 * from this one in the last bit for 0.06% of random doubles and about 0.5%
 * of the arguments in [0.5, 2) (see the differential test in tests/crt for
 * the measured rates). The log2 that reproduces UCRT bit for bit, and the
 * one behind the standard name log2 of the CRT-free layer, is xx_ms_log2
 * (xx_ms_log2.c).
 */

#include "xxfclib/crt/xx_crt.h"

#include <stdint.h>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <emmintrin.h>
#define XX_CRT_MATH_SSE2 1
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__SSE2__))
#include <emmintrin.h>
#define XX_CRT_MATH_SSE2 1
#endif

typedef union xx_crt_math_bits_u {
    double nDouble;
    uint64_t nBits;
} xx_crt_math_bits;

static uint64_t xx_crt_math_to_bits(double nValue)
{
    xx_crt_math_bits u;
    u.nDouble = nValue;
    return u.nBits;
}

static double xx_crt_math_from_bits(uint64_t nBits)
{
    xx_crt_math_bits u;
    u.nBits = nBits;
    return u.nDouble;
}

#define XX_CRT_NAN_IND 0xFFF8000000000000ull

short xx_crt_dclass(double nValue)
{
    uint64_t nBits = xx_crt_math_to_bits(nValue) & 0x7FFFFFFFFFFFFFFFull;
    if (nBits >= 0x7FF0000000000000ull) return nBits == 0x7FF0000000000000ull ? (short)1 : (short)2;
    if (nBits == 0) return 0;
    return nBits < 0x0010000000000000ull ? (short)-2 : (short)-1;
}

int xx_crt_dsign(double nValue)
{
    return (int)(xx_crt_math_to_bits(nValue) >> 63);
}

short xx_crt_fdclass(float nValue)
{
    union {
        float nFloat;
        uint32_t nBits;
    } u;
    uint32_t nBits;
    u.nFloat = nValue;
    nBits = u.nBits & 0x7FFFFFFFu;
    if (nBits >= 0x7F800000u) return nBits == 0x7F800000u ? (short)1 : (short)2;
    if (nBits == 0) return 0;
    return nBits < 0x00800000u ? (short)-2 : (short)-1;
}

int xx_crt_fdsign(float nValue)
{
    union {
        float nFloat;
        uint32_t nBits;
    } u;
    u.nFloat = nValue;
    return (int)(u.nBits >> 31);
}

double xx_crt_fabs(double nValue)
{
    return xx_crt_math_from_bits(xx_crt_math_to_bits(nValue) & 0x7FFFFFFFFFFFFFFFull);
}

/* NaN in, quiet NaN out (the payload kept). */
static double xx_crt_math_quiet(double nValue)
{
    return xx_crt_math_from_bits(xx_crt_math_to_bits(nValue) | 0x0008000000000000ull);
}

double xx_crt_trunc(double nValue)
{
    uint64_t nBits = xx_crt_math_to_bits(nValue);
    int nExponent = (int)((nBits >> 52) & 0x7FF) - 1023;
    uint64_t nMask;
    if (nExponent == 1024) return (nBits & 0x000FFFFFFFFFFFFFull) ? xx_crt_math_quiet(nValue) : nValue;
    if (nExponent < 0) return xx_crt_math_from_bits(nBits & 0x8000000000000000ull);
    if (nExponent >= 52) return nValue;
    nMask = (1ull << (52 - nExponent)) - 1;
    return xx_crt_math_from_bits(nBits & ~nMask);
}

double xx_crt_floor(double nValue)
{
    double nTruncated = xx_crt_trunc(nValue);
    if (nValue < 0 && nTruncated != nValue) return nTruncated - 1.0;
    return nTruncated;
}

double xx_crt_ceil(double nValue)
{
    double nTruncated = xx_crt_trunc(nValue);
    if (nValue > 0 && nTruncated != nValue) return nTruncated + 1.0;
    return nTruncated;
}

/* The correctly rounded square root with integer operations only: the
 * significand is scaled to an even exponent, its 54-bit root (53 bits and a
 * rounding bit) is found bit by bit, and the remainder decides the rounding.
 * Exact for every argument, subnormals included. */
double xx_crt_sqrt_portable(double nValue)
{
    uint64_t nBits = xx_crt_math_to_bits(nValue);
    uint64_t nMantissa;
    uint64_t nRoot = 0;
    uint64_t nRemainder = 0;
    int nExponent;
    int i;
    if ((nBits & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull) return xx_crt_math_quiet(nValue);
    if ((nBits >> 63) && (nBits << 1)) return xx_crt_math_from_bits(XX_CRT_NAN_IND);
    if ((nBits << 1) == 0 || nBits == 0x7FF0000000000000ull) return nValue;
    nExponent = (int)(nBits >> 52);
    nMantissa = nBits & 0x000FFFFFFFFFFFFFull;
    if (nExponent == 0) {
        nExponent = 1;
        while (!(nMantissa & 0x0010000000000000ull)) {
            nMantissa <<= 1;
            --nExponent;
        }
    } else {
        nMantissa |= 0x0010000000000000ull;
    }
    nExponent -= 1023; /* value = nMantissa * 2^(nExponent - 52) */
    if (nExponent & 1) {
        nMantissa <<= 1; /* make the exponent even */
        --nExponent;
    }
    /* root of nMantissa * 2^54 (107 or 108 bits): 54 bits, two input bits
     * per step, most significant first; the low 54 input bits are zero */
    for (i = 53; i >= 0; --i) {
        uint64_t nTrial;
        unsigned nPair = (i >= 27) ? (unsigned)((nMantissa >> (2 * (i - 27))) & 3) : 0;
        nRemainder = (nRemainder << 2) | nPair;
        nTrial = (nRoot << 2) | 1;
        nRoot <<= 1;
        if (nRemainder >= nTrial) {
            nRemainder -= nTrial;
            nRoot |= 1;
        }
    }
    /* nRoot = floor(sqrt(nMantissa * 2^54)) in [2^53, 2^54): the last bit is
     * the rounding bit, and it is never an exact tie (a nonzero remainder
     * exists whenever the rounding bit is set) */
    nRoot = (nRoot >> 1) + (nRoot & 1);
    if (nRoot >> 53) {
        nRoot >>= 1;
        nExponent += 2;
    }
    return xx_crt_math_from_bits(((uint64_t)(nExponent / 2 + 1023) << 52) | (nRoot & 0x000FFFFFFFFFFFFFull));
}

double xx_crt_sqrt(double nValue)
{
#if defined(XX_CRT_MATH_SSE2)
    uint64_t nBits = xx_crt_math_to_bits(nValue);
    if ((nBits & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull) return xx_crt_math_quiet(nValue);
    if ((nBits >> 63) && (nBits << 1)) return xx_crt_math_from_bits(XX_CRT_NAN_IND);
    return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(nValue)));
#else
    return xx_crt_sqrt_portable(nValue);
#endif
}

int xx_crt_sqrt_errno(double nValue)
{
    uint64_t nBits = xx_crt_math_to_bits(nValue);
    if ((nBits & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull) return 0;
    return ((nBits >> 63) && (nBits << 1)) ? XX_CRT_EDOM : 0;
}

int xx_crt_log2_errno(double nValue)
{
    uint64_t nBits = xx_crt_math_to_bits(nValue);
    if ((nBits & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull) return 0;
    if ((nBits << 1) == 0) return XX_CRT_ERANGE;
    return (nBits >> 63) ? XX_CRT_EDOM : 0;
}

/* -------------------------------------------------- double-double tools -- */

typedef struct xx_crt_dd_s {
    double hi;
    double lo;
} xx_crt_dd;

static xx_crt_dd xx_crt_dd_make(double hi, double lo)
{
    xx_crt_dd r;
    r.hi = hi;
    r.lo = lo;
    return r;
}

static xx_crt_dd xx_crt_dd_two_sum(double a, double b)
{
    double s = a + b;
    double bb = s - a;
    return xx_crt_dd_make(s, (a - (s - bb)) + (b - bb));
}

static xx_crt_dd xx_crt_dd_fast_two_sum(double a, double b)
{
    double s = a + b;
    return xx_crt_dd_make(s, b - (s - a));
}

static void xx_crt_dd_split(double a, double *pHi, double *pLo)
{
    double c = 134217729.0 * a; /* 2^27 + 1 */
    double h = c - (c - a);
    *pHi = h;
    *pLo = a - h;
}

static xx_crt_dd xx_crt_dd_two_prod(double a, double b)
{
    double p = a * b;
    double ah, al, bh, bl;
    xx_crt_dd_split(a, &ah, &al);
    xx_crt_dd_split(b, &bh, &bl);
    return xx_crt_dd_make(p, ((ah * bh - p) + ah * bl + al * bh) + al * bl);
}

static xx_crt_dd xx_crt_dd_add(xx_crt_dd x, xx_crt_dd y)
{
    xx_crt_dd s = xx_crt_dd_two_sum(x.hi, y.hi);
    xx_crt_dd t = xx_crt_dd_two_sum(x.lo, y.lo);
    s.lo += t.hi;
    s = xx_crt_dd_fast_two_sum(s.hi, s.lo);
    s.lo += t.lo;
    return xx_crt_dd_fast_two_sum(s.hi, s.lo);
}

static xx_crt_dd xx_crt_dd_add_d(xx_crt_dd x, double y)
{
    xx_crt_dd s = xx_crt_dd_two_sum(x.hi, y);
    s.lo += x.lo;
    return xx_crt_dd_fast_two_sum(s.hi, s.lo);
}

static xx_crt_dd xx_crt_dd_mul(xx_crt_dd x, xx_crt_dd y)
{
    xx_crt_dd p = xx_crt_dd_two_prod(x.hi, y.hi);
    p.lo += x.hi * y.lo + x.lo * y.hi;
    return xx_crt_dd_fast_two_sum(p.hi, p.lo);
}

static xx_crt_dd xx_crt_dd_mul_d(xx_crt_dd x, double y)
{
    xx_crt_dd p = xx_crt_dd_two_prod(x.hi, y);
    p.lo += x.lo * y;
    return xx_crt_dd_fast_two_sum(p.hi, p.lo);
}

static xx_crt_dd xx_crt_dd_div(xx_crt_dd x, xx_crt_dd y)
{
    double q1 = x.hi / y.hi;
    xx_crt_dd r = xx_crt_dd_add(x, xx_crt_dd_mul_d(y, -q1));
    double q2 = r.hi / y.hi;
    double q3;
    r = xx_crt_dd_add(r, xx_crt_dd_mul_d(y, -q2));
    q3 = r.hi / y.hi;
    return xx_crt_dd_add_d(xx_crt_dd_fast_two_sum(q1, q2), q3);
}

/* log2(e) as a double-double: 0x3FF71547652B82FE + 0x3C7777D0FFDA0D24. */
static const double XX_CRT_INV_LN2_HI = 1.4426950408889634;
static const double XX_CRT_INV_LN2_LO = 2.0355273740931033e-17;

/* Splits a positive finite x into m * 2^k with m in [sqrt(1/2), sqrt(2)). */
static double xx_crt_log2_reduce(double x, int *pnExponent)
{
    uint64_t nBits = xx_crt_math_to_bits(x);
    int nExponent = 0;
    double m;
    if ((nBits >> 52) == 0) {
        x *= 18014398509481984.0; /* 2^54 */
        nBits = xx_crt_math_to_bits(x);
        nExponent = -54;
    }
    nExponent += (int)(nBits >> 52) - 1023;
    m = xx_crt_math_from_bits((nBits & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull);
    if (m >= 1.4142135623730951) {
        m *= 0.5;
        ++nExponent;
    }
    *pnExponent = nExponent;
    return m;
}

/* log2(m) for m in [sqrt(1/2), sqrt(2)) with about 2^-100 relative error:
 * ln m = 2 atanh(s), s = (m - 1)/(m + 1), |s| < 0.1716. */
static xx_crt_dd xx_crt_log2_accurate_m(double m)
{
    xx_crt_dd s;
    xx_crt_dd t;
    xx_crt_dd sum;
    int n;
    if (m == 1.0) return xx_crt_dd_make(0.0, 0.0);
    s = xx_crt_dd_div(xx_crt_dd_make(m - 1.0, 0.0), xx_crt_dd_two_sum(m, 1.0));
    t = xx_crt_dd_mul(s, s);
    /* Horner over 1/(2n+1), n = 26..0: t^27 < 2^-137 */
    sum = xx_crt_dd_div(xx_crt_dd_make(1.0, 0.0), xx_crt_dd_make(53.0, 0.0));
    for (n = 25; n >= 0; --n) {
        sum = xx_crt_dd_add(xx_crt_dd_mul(sum, t), xx_crt_dd_div(xx_crt_dd_make(1.0, 0.0), xx_crt_dd_make((double)(2 * n + 1), 0.0)));
    }
    sum = xx_crt_dd_mul(sum, s);
    sum = xx_crt_dd_mul_d(sum, 2.0);
    return xx_crt_dd_mul(sum, xx_crt_dd_make(XX_CRT_INV_LN2_HI, XX_CRT_INV_LN2_LO));
}

static double xx_crt_log2_accurate(double x)
{
    int k;
    double m = xx_crt_log2_reduce(x, &k);
    xx_crt_dd y = xx_crt_log2_accurate_m(m);
    y = xx_crt_dd_add_d(y, (double)k);
    return y.hi + y.lo;
}

/* Table: for each of 512 cells (the exponent bit of m and the top 8 bits of
 * its fraction) the reciprocal r of the cell centre, rounded to double, and
 * -log2(r) as a double-double. */
typedef struct xx_crt_log2_cell_s {
    double nInverse;
    double nLogHi;
    double nLogLo;
} xx_crt_log2_cell;

static xx_crt_log2_cell s_xx_crt_log2_table[512];
static volatile long s_xx_crt_log2_ready;

static void xx_crt_log2_build(void)
{
    int i;
    for (i = 0; i < 512; ++i) {
        double nBase = (i & 256) ? 1.0 : 0.5;
        double nCentre = nBase * (1.0 + ((double)(i & 255) + 0.5) / 256.0);
        double nInverse = 1.0 / nCentre;
        int k;
        double m = xx_crt_log2_reduce(nInverse, &k);
        xx_crt_dd y = xx_crt_dd_add_d(xx_crt_log2_accurate_m(m), (double)k);
        s_xx_crt_log2_table[i].nInverse = nInverse;
        s_xx_crt_log2_table[i].nLogHi = -y.hi;
        s_xx_crt_log2_table[i].nLogLo = -y.lo;
    }
}

#if defined(_MSC_VER)
#include <intrin.h>
static long xx_crt_log2_cas(volatile long *p, long v, long c)
{
    return _InterlockedCompareExchange(p, v, c);
}
static void xx_crt_log2_pause(void)
{
    _mm_pause();
}
#elif defined(__GNUC__) || defined(__clang__)
static long xx_crt_log2_cas(volatile long *p, long v, long c)
{
    return __sync_val_compare_and_swap(p, c, v);
}
static void xx_crt_log2_pause(void)
{
}
#else
static long xx_crt_log2_cas(volatile long *p, long v, long c)
{
    long o = *p;
    if (o == c) *p = v;
    return o;
}
static void xx_crt_log2_pause(void)
{
}
#endif

/* 0 = not built, 1 = building, 2 = ready. */
static void xx_crt_log2_ensure_table(void)
{
    if (s_xx_crt_log2_ready == 2) return;
    if (xx_crt_log2_cas(&s_xx_crt_log2_ready, 1, 0) == 0) {
        xx_crt_log2_build();
#if defined(_MSC_VER)
        _InterlockedExchange(&s_xx_crt_log2_ready, 2);
#else
        __sync_lock_test_and_set(&s_xx_crt_log2_ready, 2);
#endif
        return;
    }
    while (s_xx_crt_log2_ready != 2) xx_crt_log2_pause();
}

double xx_crt_log2(double x)
{
    uint64_t nBits = xx_crt_math_to_bits(x);
    int k;
    double m;
    xx_crt_dd r;
    xx_crt_dd y;
    double nTail;

    /* special cases, as UCRT returns them */
    if ((nBits >> 63) || nBits >= 0x7FF0000000000000ull) {
        if ((nBits << 1) == 0) return xx_crt_math_from_bits(0xFFF0000000000000ull); /* log2(+-0) = -inf */
        if ((nBits & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull) return xx_crt_math_quiet(x);
        if (nBits == 0x7FF0000000000000ull) return x;
        return xx_crt_math_from_bits(XX_CRT_NAN_IND);
    }
    if (nBits == 0) return xx_crt_math_from_bits(0xFFF0000000000000ull);

    m = xx_crt_log2_reduce(x, &k);
    if (m == 1.0) return (double)k;

    if (m > 0.99609375 && m < 1.00390625) {
        /* near 1: r = m - 1 exactly, no table (avoids cancellation) */
        r = xx_crt_dd_make(m - 1.0, 0.0);
    } else {
        uint64_t nM = xx_crt_math_to_bits(m);
        int nIndex = (int)((nM >> 44) & 255) | ((nM >> 52) == 0x3FF ? 256 : 0);
        const xx_crt_log2_cell *pCell;
        xx_crt_dd p;
        xx_crt_log2_ensure_table();
        pCell = &s_xx_crt_log2_table[nIndex];
        p = xx_crt_dd_two_prod(m, pCell->nInverse); /* exact */
        r = xx_crt_dd_two_sum(p.hi - 1.0, p.lo);   /* p.hi - 1 is exact */
        y = xx_crt_dd_make(pCell->nLogHi, pCell->nLogLo);
        goto have_table;
    }
    y = xx_crt_dd_make(0.0, 0.0);
have_table:
    {
        /* ln(1 + r) = r - r^2/2 + r^3 (1/3 - r/4 + r^2/5 - ...), |r| < 2^-8 */
        xx_crt_dd r2 = xx_crt_dd_mul(r, r);
        xx_crt_dd lnr = xx_crt_dd_add(r, xx_crt_dd_make(-0.5 * r2.hi, -0.5 * r2.lo));
        double h = r.hi;
        nTail = h * h * h *
                (1.0 / 3 - h * (0.25 - h * (0.2 - h * (1.0 / 6 - h * (1.0 / 7 - h * (0.125 - h * (1.0 / 9 - h * (0.1 - h * (1.0 / 11)))))))));
        lnr = xx_crt_dd_add_d(lnr, nTail);
        lnr = xx_crt_dd_mul(lnr, xx_crt_dd_make(XX_CRT_INV_LN2_HI, XX_CRT_INV_LN2_LO));
        y = xx_crt_dd_add(y, lnr);
        y = xx_crt_dd_add_d(y, (double)k);
    }
    {
        /* Ziv's rounding test: y.hi is the answer unless the error bound
         * (2^-66 |y|, a margin over the 2^-69 analysis) reaches the
         * half-ulp boundary. */
        double nHi = y.hi;
        double nLo = y.lo;
        uint64_t nHiBits = xx_crt_math_to_bits(nHi) & 0x7FFFFFFFFFFFFFFFull;
        double nUlp = xx_crt_math_from_bits((nHiBits & 0x7FF0000000000000ull) - (52ull << 52));
        double nHalf = 0.5 * nUlp;
        double nError = xx_crt_fabs(nHi) * 1.3552527156068805e-20; /* 2^-66 */
        if ((nHiBits & 0x000FFFFFFFFFFFFFull) == 0 && ((nLo < 0) != (nHi < 0))) nHalf *= 0.5;
        if ((nHiBits >> 52) > 53 && xx_crt_fabs(nLo) + nError < nHalf) return nHi;
    }
    return xx_crt_log2_accurate(x);
}
