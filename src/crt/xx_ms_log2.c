/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_ms_log2.c
 * @brief xx_ms_log2: log2 with the exact results of Microsoft UCRT (x64).
 *
 * Credit. Algorithm: P. T. P. Tang's table-driven logarithm ("Table-driven
 * implementation of the logarithm function in IEEE floating-point
 * arithmetic", ACM TOMS 16(4), 1990); series coefficients and the 1/ln2
 * split as in AMD's open-source LibM; behaviour matched to Microsoft UCRT
 * log2 by black-box testing only (no Microsoft code used).
 *
 * See include/xxfclib/crt/xx_ms_log2.h for what, why and the test evidence,
 * and docs/CRT.md for the derivation. This file is the algorithm; every
 * operation below is a plain IEEE-754 double operation in round-to-nearest,
 * in exactly this order (the order is part of the result).
 *
 * Constants
 *   L + T = 1/ln(2): L = 0x3FF7154400000000 (the leading 20 bits),
 *                    T = 0x3ECB295C17F0BBBE (the rest, truncated).
 *   table[j] = ln(1 + j/64) = lead + tail for j = 0..64, where
 *     lead = floor(ln(1 + j/64) * 2^24) / 2^24   (so L * lead is exact),
 *     tail = the double nearest to ln(1 + j/64) - lead.
 *
 * Near 1: bits(x) in [0x3FEE0FAA00000000, 0x3FF1082C00000000), which is
 * about [exp(-1/16), exp(1/16)):
 *   r  = x - 1                                   (exact)
 *   u  = r / (2 + r);  c = r * u;  u = u + u;  v = u * u
 *   z  = u * v * (A1 + v * (A2 + v * (A3 + v * A4))) - c
 *        (ln(x) = r + z: the atanh series 2s + 2s^3/3 + ... in u = 2s)
 *   r1 = r with the low 32 bits cleared;  r2 = (r - r1) + z
 *   result = L * r1 + ((T * r1 + T * r2) + L * r2)
 *
 * Elsewhere: x = 2^e * m with m in [1, 2) (subnormals normalised first):
 *   j  = the leading 6 fraction bits of m, plus the 7th as a round bit
 *   F  = (64 + j) / 128;  Y = m / 2;  f = Y - F  (exact, |f| <= 1/256)
 *   u  = f / (F + f / 2);  v = u * u
 *   p  = v * (B1 + v * (B2 + v * B3))
 *   z1 = lead[j];  z2 = tail[j] + (u + u * p)    (ln(m) = z1 + z2)
 *   result = (e + L * z1) + ((T * z1 + T * z2) + L * z2)
 */

#include "xxfclib/crt/xx_ms_log2.h"

#include <stdint.h>

/* The result depends on every single rounding: a*b+c must not be fused. */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma fp_contract(off)
#pragma float_control(precise, on)
#elif defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#pragma GCC optimize("no-fast-math")
#endif

typedef union xx_ms_log2_bits_u {
    uint64_t nBits;
    double nDouble;
} xx_ms_log2_bits;

typedef struct xx_ms_log2_cell_s {
    xx_ms_log2_bits lead;
    xx_ms_log2_bits tail;
} xx_ms_log2_cell;

/* ln(1 + j/64) = lead + tail; lead has 24 fraction bits. */
static const xx_ms_log2_cell s_xx_ms_log2_table[65] = {
    {{0x0000000000000000ull}, {0x0000000000000000ull}}, /*  0: ln(64/64) */
    {{0x3F8FC0A800000000ull}, {0x3E361F807C79F3DBull}}, /*  1: ln(65/64) */
    {{0x3F9F829800000000ull}, {0x3E6873C1980267C8ull}}, /*  2: ln(66/64) */
    {{0x3FA7745800000000ull}, {0x3E5EC65B9F88C69Eull}}, /*  3: ln(67/64) */
    {{0x3FAF0A3000000000ull}, {0x3E58022C54CC2F99ull}}, /*  4: ln(68/64) */
    {{0x3FB341D700000000ull}, {0x3E62C37A3A125330ull}}, /*  5: ln(69/64) */
    {{0x3FB6F0D200000000ull}, {0x3E615CAD69737C93ull}}, /*  6: ln(70/64) */
    {{0x3FBA926D00000000ull}, {0x3E4D256AB1B285E9ull}}, /*  7: ln(71/64) */
    {{0x3FBE270700000000ull}, {0x3E5B8ABCB97A7AA2ull}}, /*  8: ln(72/64) */
    {{0x3FC0D77E00000000ull}, {0x3E6F34239659A5DCull}}, /*  9: ln(73/64) */
    {{0x3FC2955280000000ull}, {0x3E6E07FD48D30177ull}}, /* 10: ln(74/64) */
    {{0x3FC44D2B00000000ull}, {0x3E6B32DF4799F4F6ull}}, /* 11: ln(75/64) */
    {{0x3FC5FF3000000000ull}, {0x3E6C29E4F4F21CF9ull}}, /* 12: ln(76/64) */
    {{0x3FC7AB8900000000ull}, {0x3E1086C848DF1B59ull}}, /* 13: ln(77/64) */
    {{0x3FC9525A80000000ull}, {0x3E4CF456B4764130ull}}, /* 14: ln(78/64) */
    {{0x3FCAF3C900000000ull}, {0x3E63A02FFCB63398ull}}, /* 15: ln(79/64) */
    {{0x3FCC8FF780000000ull}, {0x3E61E6A6886B0976ull}}, /* 16: ln(80/64) */
    {{0x3FCE270700000000ull}, {0x3E6B8ABCB97A7AA2ull}}, /* 17: ln(81/64) */
    {{0x3FCFB91800000000ull}, {0x3E6B578F8AA35552ull}}, /* 18: ln(82/64) */
    {{0x3FD0A324C0000000ull}, {0x3E6139C871AFB9FCull}}, /* 19: ln(83/64) */
    {{0x3FD1675C80000000ull}, {0x3E65D5D30701CE64ull}}, /* 20: ln(84/64) */
    {{0x3FD22941C0000000ull}, {0x3E6DE7BCB2D12143ull}}, /* 21: ln(85/64) */
    {{0x3FD2E8E280000000ull}, {0x3E6D708E984E1665ull}}, /* 22: ln(86/64) */
    {{0x3FD3A64C40000000ull}, {0x3E556945E9C72F36ull}}, /* 23: ln(87/64) */
    {{0x3FD4618BC0000000ull}, {0x3E20E2F613E85BDAull}}, /* 24: ln(88/64) */
    {{0x3FD51AAD80000000ull}, {0x3E3CB7E0B42724F6ull}}, /* 25: ln(89/64) */
    {{0x3FD5D1BD80000000ull}, {0x3E6FAC04E52846C7ull}}, /* 26: ln(90/64) */
    {{0x3FD686C800000000ull}, {0x3E5E9B14AEC442BEull}}, /* 27: ln(91/64) */
    {{0x3FD739D7C0000000ull}, {0x3E6B5DE8034E7126ull}}, /* 28: ln(92/64) */
    {{0x3FD7EAF800000000ull}, {0x3E6DC157E1B259D3ull}}, /* 29: ln(93/64) */
    {{0x3FD89A3380000000ull}, {0x3E3B05096AD69C62ull}}, /* 30: ln(94/64) */
    {{0x3FD9479400000000ull}, {0x3E5C2116FABA4CDDull}}, /* 31: ln(95/64) */
    {{0x3FD9F323C0000000ull}, {0x3E665FCC25F95B47ull}}, /* 32: ln(96/64) */
    {{0x3FDA9CEC80000000ull}, {0x3E5A9A08498D4850ull}}, /* 33: ln(97/64) */
    {{0x3FDB44F740000000ull}, {0x3E6DE647B1465F77ull}}, /* 34: ln(98/64) */
    {{0x3FDBEB4D80000000ull}, {0x3E5DA71B7BF7861Dull}}, /* 35: ln(99/64) */
    {{0x3FDC8FF7C0000000ull}, {0x3E3E6A6886B09760ull}}, /* 36: ln(100/64) */
    {{0x3FDD32FE40000000ull}, {0x3E6F0075EAB0EF64ull}}, /* 37: ln(101/64) */
    {{0x3FDDD46A00000000ull}, {0x3E33071282FB989Bull}}, /* 38: ln(102/64) */
    {{0x3FDE744240000000ull}, {0x3E60EB43C3F1BED2ull}}, /* 39: ln(103/64) */
    {{0x3FDF128F40000000ull}, {0x3E5FAF06ECB35C84ull}}, /* 40: ln(104/64) */
    {{0x3FDFAF5880000000ull}, {0x3E4EF1E63DB35F68ull}}, /* 41: ln(105/64) */
    {{0x3FE02552A0000000ull}, {0x3E469743FB1A71A5ull}}, /* 42: ln(106/64) */
    {{0x3FE0723E40000000ull}, {0x3E6C1CDF404E5796ull}}, /* 43: ln(107/64) */
    {{0x3FE0BE72E0000000ull}, {0x3E4094AA0ADA625Full}}, /* 44: ln(108/64) */
    {{0x3FE109F380000000ull}, {0x3E6E2D4C96FDE3EDull}}, /* 45: ln(109/64) */
    {{0x3FE154C3C0000000ull}, {0x3E62F4D5E9A98F34ull}}, /* 46: ln(110/64) */
    {{0x3FE19EE6A0000000ull}, {0x3E6467C96ECC5CBEull}}, /* 47: ln(111/64) */
    {{0x3FE1E85F40000000ull}, {0x3E6E7040D03DEC5Aull}}, /* 48: ln(112/64) */
    {{0x3FE23130C0000000ull}, {0x3E67BEBF4282DE37ull}}, /* 49: ln(113/64) */
    {{0x3FE2795E00000000ull}, {0x3E6289B11AEB783Full}}, /* 50: ln(114/64) */
    {{0x3FE2C0E9E0000000ull}, {0x3E5A891D1772F538ull}}, /* 51: ln(115/64) */
    {{0x3FE307D720000000ull}, {0x3E634F10BE1FB591ull}}, /* 52: ln(116/64) */
    {{0x3FE34E2880000000ull}, {0x3E6D9CE1D316EB93ull}}, /* 53: ln(117/64) */
    {{0x3FE393E0C0000000ull}, {0x3E63562A19A9C442ull}}, /* 54: ln(118/64) */
    {{0x3FE3D90260000000ull}, {0x3E54E2ADF548084Cull}}, /* 55: ln(119/64) */
    {{0x3FE41D8FE0000000ull}, {0x3E508CE55CC8C97Aull}}, /* 56: ln(120/64) */
    {{0x3FE4618BC0000000ull}, {0x3E30E2F613E85BDAull}}, /* 57: ln(121/64) */
    {{0x3FE4A4F840000000ull}, {0x3E6DB03EBB0227BFull}}, /* 58: ln(122/64) */
    {{0x3FE4E7D800000000ull}, {0x3E61B75BB09CB098ull}}, /* 59: ln(123/64) */
    {{0x3FE52A2D20000000ull}, {0x3E496F16ABB9DF23ull}}, /* 60: ln(124/64) */
    {{0x3FE56BF9C0000000ull}, {0x3E65B3F399411C62ull}}, /* 61: ln(125/64) */
    {{0x3FE5AD4040000000ull}, {0x3E586B3E59F65355ull}}, /* 62: ln(126/64) */
    {{0x3FE5EE02A0000000ull}, {0x3E52482CEAE1AC13ull}}, /* 63: ln(127/64) */
    {{0x3FE62E42E0000000ull}, {0x3E6EFA39EF35793Cull}}, /* 64: ln(128/64) */
};

static const xx_ms_log2_bits s_xx_ms_log2_l = {0x3FF7154400000000ull}; /* 1/ln2, leading part */
static const xx_ms_log2_bits s_xx_ms_log2_t = {0x3ECB295C17F0BBBEull}; /* 1/ln2, trailing part */

/* near-1 series: 1/12, 1/80, 1/448, 1/2304 as the published minimax fit */
static const xx_ms_log2_bits s_xx_ms_log2_a1 = {0x3FB55555555554E6ull};
static const xx_ms_log2_bits s_xx_ms_log2_a2 = {0x3F89999999BAC6D4ull};
static const xx_ms_log2_bits s_xx_ms_log2_a3 = {0x3F62492307F1519Full};
static const xx_ms_log2_bits s_xx_ms_log2_a4 = {0x3F3C8034C85DFFF0ull};

/* table-path series: 1/12, 1/80, 1/448 */
static const xx_ms_log2_bits s_xx_ms_log2_b1 = {0x3FB5555555555557ull};
static const xx_ms_log2_bits s_xx_ms_log2_b2 = {0x3F89999999865EDEull};
static const xx_ms_log2_bits s_xx_ms_log2_b3 = {0x3F6249423BD94741ull};

#define XX_MS_LOG2_NEAR_LOW 0x3FEE0FAA00000000ull
#define XX_MS_LOG2_NEAR_HIGH 0x3FF1082C00000000ull

double xx_ms_log2(double nValue)
{
    xx_ms_log2_bits x;
    uint64_t nBits;
    uint64_t nFraction;
    int nExponent;
    unsigned int nIndex;
    const double nL = s_xx_ms_log2_l.nDouble;
    const double nT = s_xx_ms_log2_t.nDouble;

    x.nDouble = nValue;
    nBits = x.nBits;

    /* special values, as UCRT returns them */
    if ((nBits >> 63) || nBits >= 0x7FF0000000000000ull) {
        if ((nBits << 1) == 0) {
            x.nBits = 0xFFF0000000000000ull; /* log2(-0) = -inf */
        } else if ((nBits & 0x7FFFFFFFFFFFFFFFull) > 0x7FF0000000000000ull) {
            x.nBits = nBits | 0x0008000000000000ull; /* NaN: quiet, payload kept */
        } else if (nBits != 0x7FF0000000000000ull) {
            x.nBits = 0xFFF8000000000000ull; /* negative: -nan(ind) */
        }
        return x.nDouble;
    }
    if (nBits == 0) {
        x.nBits = 0xFFF0000000000000ull; /* log2(+0) = -inf */
        return x.nDouble;
    }

    if (nBits - XX_MS_LOG2_NEAR_LOW < XX_MS_LOG2_NEAR_HIGH - XX_MS_LOG2_NEAR_LOW) {
        xx_ms_log2_bits r1;
        double r = nValue - 1.0;
        double u = r / (2.0 + r);
        double nCorrection = r * u;
        double v;
        double z;
        double r2;
        u = u + u;
        v = u * u;
        z = u * v *
                (s_xx_ms_log2_a1.nDouble +
                 v * (s_xx_ms_log2_a2.nDouble + v * (s_xx_ms_log2_a3.nDouble + v * s_xx_ms_log2_a4.nDouble))) -
            nCorrection;
        r1.nDouble = r;
        r1.nBits &= 0xFFFFFFFF00000000ull;
        r2 = (r - r1.nDouble) + z;
        return nL * r1.nDouble + ((nT * r1.nDouble + nT * r2) + nL * r2);
    }

    nExponent = (int)(nBits >> 52);
    nFraction = nBits & 0x000FFFFFFFFFFFFFull;
    if (nExponent == 0) {
        /* subnormal: shift the leading one up to bit 52 */
        int nShift = 0;
        while (!(nFraction & 0x0010000000000000ull)) {
            nFraction <<= 1;
            ++nShift;
        }
        nFraction &= 0x000FFFFFFFFFFFFFull;
        nExponent = 1 - nShift;
    }
    nExponent -= 1023;

    nIndex = (unsigned int)(nFraction >> 46) + (unsigned int)((nFraction >> 45) & 1);
    {
        xx_ms_log2_bits y;
        double nF = (double)(64 + nIndex) * 0.0078125; /* (64 + j) / 128, exact */
        double f;
        double u;
        double v;
        double nPoly;
        double z1 = s_xx_ms_log2_table[nIndex].lead.nDouble;
        double z2;
        y.nBits = nFraction | 0x3FE0000000000000ull; /* m / 2 in [0.5, 1) */
        f = y.nDouble - nF;
        u = f / (nF + 0.5 * f);
        v = u * u;
        nPoly = v * (s_xx_ms_log2_b1.nDouble + v * (s_xx_ms_log2_b2.nDouble + v * s_xx_ms_log2_b3.nDouble));
        z2 = s_xx_ms_log2_table[nIndex].tail.nDouble + (u + u * nPoly);
        return ((double)nExponent + nL * z1) + ((nT * z1 + nT * z2) + nL * z2);
    }
}
