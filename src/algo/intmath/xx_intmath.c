/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_intmath.c
 * @brief Implementation of integer math algorithms.
 */

#include "xxfclib/algo/intmath/xx_intmath.h"
#include <stdlib.h>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
#include <intrin.h>
#define xx_umul128_hi(a, b) __umulh((uint64_t)(a), (uint64_t)(b))
#define xx_smul128_hi(a, b) __mulh((int64_t)(a), (int64_t)(b))
#elif defined(__SIZEOF_INT128__)
#define xx_umul128_hi(a, b) ((uint64_t)(((unsigned __int128)(uint64_t)(a) * (uint64_t)(b)) >> 64))
#define xx_smul128_hi(a, b) ((int64_t)(((__int128)(int64_t)(a) * (int64_t)(b)) >> 64))
#else
static uint64_t xx_umul128_hi(uint64_t u, uint64_t v)
{
    uint64_t u0 = u & 0xFFFFFFFFU, u1 = u >> 32;
    uint64_t v0 = v & 0xFFFFFFFFU, v1 = v >> 32;
    uint64_t w0 = u0 * v0;
    uint64_t t = u1 * v0 + (w0 >> 32);
    uint64_t w1 = t & 0xFFFFFFFFU;
    uint64_t w2 = t >> 32;
    w1 += u0 * v1;
    return u1 * v1 + w2 + (w1 >> 32);
}
static int64_t xx_smul128_hi(int64_t u, int64_t v)
{
    uint64_t hi = xx_umul128_hi((uint64_t)u, (uint64_t)v);
    if (u < 0) hi -= (uint64_t)v;
    if (v < 0) hi -= (uint64_t)u;
    return (int64_t)hi;
}
#endif

/* ========================================================================= */
/* --- 1. GCD, Extended Euclid, and Modular Inverse                      --- */
/* ========================================================================= */

uint64_t xx_gcd_u64(uint64_t a, uint64_t b)
{
    while (b != 0) {
        uint64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

/* Magnitude of a signed value as an unsigned one (2^63 for INT64_MIN). */
static uint64_t xx_abs_u64(int64_t v)
{
    return v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
}

/* Arithmetic right shift that does not depend on how the compiler shifts a
 * negative value. */
static int32_t xx_sar32(int32_t v, uint32_t s)
{
    return v >= 0 ? (v >> s) : ~(~v >> s);
}

static int64_t xx_sar64(int64_t v, uint32_t s)
{
    return v >= 0 ? (v >> s) : ~(~v >> s);
}

int64_t xx_ext_euclid_i64(int64_t a, int64_t b, int64_t *x, int64_t *y)
{
    /* The remainders are the magnitudes (so INT64_MIN is fine); the Bezout
     * coefficients are kept modulo 2^64. The returned pair satisfies
     * |x| <= |b| / (2g) and |y| <= |a| / (2g) (or is 0 / +-1), so it is exact;
     * only the discarded next pair (|b| / g, |a| / g) can reach 2^63, which
     * the unsigned arithmetic absorbs. */
    uint64_t ua = xx_abs_u64(a);
    uint64_t ub = xx_abs_u64(b);
    uint64_t s0 = 1, s1 = 0;
    uint64_t t0 = 0, t1 = 1;

    while (ub != 0) {
        uint64_t q = ua / ub;
        uint64_t r = ua % ub;
        ua = ub;
        ub = r;

        uint64_t s2 = s0 - q * s1;
        s0 = s1;
        s1 = s2;

        uint64_t t2 = t0 - q * t1;
        t0 = t1;
        t1 = t2;
    }

    if (a < 0) s0 = (uint64_t)0 - s0;
    if (b < 0) t0 = (uint64_t)0 - t0;
    if (x) *x = (int64_t)s0;
    if (y) *y = (int64_t)t0;
    return (int64_t)ua;
}

bool xx_mod_inverse_u64(uint64_t a, uint64_t m, uint64_t *inv)
{
    if (m <= 1 || !inv) return false;

    /* Extended Euclid on (m, a mod m), tracking only the coefficient of a.
     * Its sign alternates from step to step, so the magnitudes are kept
     * unsigned: t(k+1) = t(k-1) + q * t(k). They grow up to m / gcd <= m, so
     * nothing overflows for any 64-bit modulus. */
    uint64_t r0 = m, r1 = a % m;
    uint64_t t0 = 0, t1 = 1;
    bool neg0 = false, neg1 = false;

    while (r1 != 0) {
        uint64_t q = r0 / r1;
        uint64_t r2 = r0 - q * r1;
        uint64_t t2 = t0 + q * t1;
        r0 = r1;
        r1 = r2;
        t0 = t1;
        t1 = t2;
        neg0 = neg1;
        neg1 = !neg1;
    }

    if (r0 != 1) return false;
    *inv = neg0 ? m - t0 : t0;
    return true;
}

/* ========================================================================= */
/* --- 2. Division by Invariant Multiplication                           --- */
/* ========================================================================= */

bool xx_magic_compute_u32(uint32_t d, xx_magic_u32 *out)
{
    if (d == 0 || !out) return false;
    if (d == 1) {
        out->magic = 0;
        out->shift = 0;
        out->add_flag = true;
        return true;
    }

    /* Hacker's Delight section 10-1 */
    uint64_t nc = (UINT64_C(1) << 32) - 1 - (((UINT64_C(1) << 32) - 1 - d) % d);
    uint32_t p = 31;
    uint64_t q1 = (UINT64_C(1) << 31) / nc;
    uint64_t r1 = (UINT64_C(1) << 31) - q1 * nc;
    uint64_t q2 = ((UINT64_C(1) << 31) - 1) / d;
    uint64_t r2 = ((UINT64_C(1) << 31) - 1) - q2 * d;
    uint64_t delta;

    do {
        p = p + 1;
        if (r1 >= nc - r1) {
            q1 = 2 * q1 + 1;
            r1 = 2 * r1 - nc;
        } else {
            q1 = 2 * q1;
            r1 = 2 * r1;
        }
        if (r2 + 1 >= d - r2) {
            q2 = 2 * q2 + 1;
            r2 = 2 * r2 + 1 - d;
        } else {
            q2 = 2 * q2;
            r2 = 2 * r2 + 1;
        }
        delta = d - 1 - r2;
    } while (p < 64 && (q1 < delta || (q1 == delta && r1 == 0)));

    uint64_t M = q2 + 1;
    if (M >= (UINT64_C(1) << 32)) {
        out->magic = (uint32_t)(M - (UINT64_C(1) << 32));
        out->add_flag = true;
    } else {
        out->magic = (uint32_t)M;
        out->add_flag = false;
    }
    out->shift = p - 32;
    return true;
}

uint32_t xx_magic_divide_u32(uint32_t n, const xx_magic_u32 *m)
{
    if (!m) return 0;
    if (m->add_flag && m->magic == 0 && m->shift == 0) return n; /* d = 1 */
    if (!m->add_flag) {
        return (uint32_t)(((uint64_t)m->magic * n) >> (32 + m->shift));
    } else {
        uint32_t t = (uint32_t)(((uint64_t)m->magic * n) >> 32);
        return (t + ((n - t) >> 1)) >> (m->shift > 0 ? m->shift - 1 : 0);
    }
}

bool xx_magic_compute_s32(int32_t d, xx_magic_s32 *out)
{
    if (d == 0 || d == 1 || d == -1 || !out) return false;

    /* Hacker's Delight, Figure 10-1 (magic). Valid for 2 <= d <= 2^31 - 1
     * and -2^31 <= d <= -2; everything is unsigned 32-bit arithmetic. */
    const uint32_t two31 = UINT32_C(0x80000000);
    uint32_t ad = d < 0 ? (uint32_t)0 - (uint32_t)d : (uint32_t)d;
    uint32_t t = two31 + ((uint32_t)d >> 31);
    uint32_t anc = t - 1 - t % ad; /* |nc|; t is 2^31 for d > 0 and 2^31 + 1 for d < 0 */
    uint32_t p = 31;
    uint32_t q1 = two31 / anc;
    uint32_t r1 = two31 - q1 * anc;
    uint32_t q2 = two31 / ad;
    uint32_t r2 = two31 - q2 * ad;
    uint32_t delta;

    do {
        p = p + 1;
        q1 = 2 * q1;
        r1 = 2 * r1;
        if (r1 >= anc) {
            q1 += 1;
            r1 -= anc;
        }
        q2 = 2 * q2;
        r2 = 2 * r2;
        if (r2 >= ad) {
            q2 += 1;
            r2 -= ad;
        }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    uint32_t M = q2 + 1;
    if (d < 0) M = (uint32_t)0 - M;
    out->shift = p - 32;
    out->magic = (int32_t)M;
    return true;
}

int32_t xx_magic_divide_s32(int32_t n, int32_t d, const xx_magic_s32 *m)
{
    if (!m) return 0;
    /* q = mulhs(M, n), corrected by -+n when the signs of d and M differ
     * (modulo 2^32, as in the machine sequence), shifted, plus 1 if negative. */
    uint32_t q = (uint32_t)xx_sar64((int64_t)m->magic * n, 32);
    if (d > 0 && m->magic < 0) q += (uint32_t)n;
    if (d < 0 && m->magic > 0) q -= (uint32_t)n;
    q = (uint32_t)xx_sar32((int32_t)q, m->shift);
    q += q >> 31;
    return (int32_t)q;
}

bool xx_magic_compute_u64(uint64_t d, xx_magic_u64 *out)
{
    if (d == 0 || !out) return false;
    if (d == 1) {
        out->magic = 0;
        out->shift = 0;
        out->add_flag = true;
        return true;
    }

    /* Algorithm for 64-bit */
    uint64_t nc = UINT64_MAX - (UINT64_MAX - d) % d;
    uint32_t p = 63;
    uint64_t q1_hi = 0, q1_lo = (UINT64_C(1) << 63) / nc;
    uint64_t r1 = (UINT64_C(1) << 63) - q1_lo * nc;
    uint64_t q2_hi = 0, q2_lo = ((UINT64_C(1) << 63) - 1) / d;
    uint64_t r2 = ((UINT64_C(1) << 63) - 1) - q2_lo * d;
    uint64_t delta;

    do {
        p = p + 1;
        uint64_t carry1 = 0;
        if (r1 >= nc - r1) {
            carry1 = 1;
            r1 = 2 * r1 - nc;
        } else {
            r1 = 2 * r1;
        }
        uint64_t q1_c = (q1_lo >> 63) & 1;
        q1_lo = (q1_lo << 1) + carry1;
        q1_hi = (q1_hi << 1) + q1_c;

        uint64_t carry2 = 0;
        if (r2 + 1 >= d - r2) {
            carry2 = 1;
            r2 = 2 * r2 + 1 - d;
        } else {
            r2 = 2 * r2 + 1;
        }
        uint64_t q2_c = (q2_lo >> 63) & 1;
        q2_lo = (q2_lo << 1) + carry2;
        q2_hi = (q2_hi << 1) + q2_c;

        delta = d - 1 - r2;
    } while (p < 128 && (q1_hi == 0 && (q1_lo < delta || (q1_lo == delta && r1 == 0))));

    uint64_t M_lo = q2_lo + 1;
    uint64_t M_hi = q2_hi + (M_lo == 0 ? 1 : 0);
    if (M_hi > 0) {
        out->magic = M_lo;
        out->add_flag = true;
    } else {
        out->magic = M_lo;
        out->add_flag = false;
    }
    out->shift = p - 64;
    return true;
}

uint64_t xx_magic_divide_u64(uint64_t n, const xx_magic_u64 *m)
{
    if (!m) return 0;
    if (m->add_flag && m->magic == 0 && m->shift == 0) return n; /* d = 1 */
    if (!m->add_flag) {
        uint64_t hi = xx_umul128_hi(m->magic, n);
        return hi >> m->shift;
    } else {
        uint64_t t = xx_umul128_hi(m->magic, n);
        return (t + ((n - t) >> 1)) >> (m->shift > 0 ? m->shift - 1 : 0);
    }
}

bool xx_magic_compute_s64(int64_t d, xx_magic_s64 *out)
{
    if (d == 0 || d == 1 || d == -1 || !out) return false;

    /* Hacker's Delight, Figure 10-1 (magic), widened to 64 bits. Valid for
     * 2 <= d <= 2^63 - 1 and -2^63 <= d <= -2. */
    const uint64_t two63 = UINT64_C(0x8000000000000000);
    uint64_t ad = xx_abs_u64(d);
    uint64_t t = two63 + ((uint64_t)d >> 63);
    uint64_t anc = t - 1 - t % ad;
    uint32_t p = 63;
    uint64_t q1 = two63 / anc;
    uint64_t r1 = two63 - q1 * anc;
    uint64_t q2 = two63 / ad;
    uint64_t r2 = two63 - q2 * ad;
    uint64_t delta;

    do {
        p = p + 1;
        q1 = 2 * q1;
        r1 = 2 * r1;
        if (r1 >= anc) {
            q1 += 1;
            r1 -= anc;
        }
        q2 = 2 * q2;
        r2 = 2 * r2;
        if (r2 >= ad) {
            q2 += 1;
            r2 -= ad;
        }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    uint64_t M = q2 + 1;
    if (d < 0) M = (uint64_t)0 - M;
    out->shift = p - 64;
    out->magic = (int64_t)M;
    return true;
}

int64_t xx_magic_divide_s64(int64_t n, int64_t d, const xx_magic_s64 *m)
{
    if (!m) return 0;
    uint64_t q = (uint64_t)xx_smul128_hi(m->magic, n);
    if (d > 0 && m->magic < 0) q += (uint64_t)n;
    if (d < 0 && m->magic > 0) q -= (uint64_t)n;
    q = (uint64_t)xx_sar64((int64_t)q, m->shift);
    q += q >> 63;
    return (int64_t)q;
}
