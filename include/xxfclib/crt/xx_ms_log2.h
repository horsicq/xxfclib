/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_ms_log2.h
 * @brief xx_ms_log2: the base-2 logarithm with exactly the results of the
 *        log2 of Microsoft's Universal C Runtime (x64), bit for bit.
 *
 * What it is
 * ----------
 * xx_ms_log2(x) returns, for every double x, the same 64 bits that log2(x)
 * of the x64 Universal CRT returns (ucrtbase.dll and the static libucrt of
 * the Windows SDK give the same results). It needs no C runtime: only
 * IEEE-754 double additions, multiplications and two divisions, a table of
 * 65 pairs and integer operations.
 *
 * Why it exists
 * -------------
 * UCRT's log2 is not correctly rounded: about 0.06% of random arguments
 * (0.5% of arguments in [0.5, 2)) come back one unit in the last place away
 * from the rounded true value. A program that must reproduce the output of
 * another program built with MSVC therefore cannot use a "better" log2.
 * The case that made this function necessary is YARA-X: its official
 * Windows binaries link the static UCRT, so math.entropy(), which sums
 * -p*log2(p) over the byte histogram, carries UCRT's rounding in its last
 * bits. xxyara has to print the same digits and decide `math.entropy(...)
 * >= x` the same way, without linking any Microsoft runtime. The CRT-free
 * standard-name layer (src/crt/platforms/xx_crt_windows.c) defines log2 as
 * xx_ms_log2 for exactly this reason.
 *
 * How it relates to xx_crt_log2
 * -----------------------------
 * xx_crt_log2 (xx_crt.h) is the correctly rounded base-2 logarithm: it
 * returns the double nearest to the true value for every argument. It is
 * the right function when no compatibility with a Microsoft build is
 * needed. xx_ms_log2 is the right function when the result has to equal
 * what an MSVC-built program computes. The two agree on all special values
 * and on about 99.94% of random doubles; where they differ, they differ by
 * exactly one unit in the last place, and xx_ms_log2 is the less accurate
 * one (its largest observed error is about 0.75 ulp). Neither function
 * changes the other; both stay available.
 *
 * How it was made (clean room)
 * ----------------------------
 * Microsoft's libm source is not available and nothing was taken from it,
 * and the binary was not disassembled. The function was reconstructed by
 * black-box testing against ucrtbase.dll / libucrt:
 *  - the error of UCRT's results against a 200-bit reference shows 64 table
 *    intervals per binade and a separate path for arguments near 1;
 *  - that is the shape of P. T. P. Tang's table-driven logarithm ("Table-
 *    driven implementation of the logarithm function in IEEE floating-point
 *    arithmetic", ACM TOMS 16(4), 1990) as published in AMD's open-source
 *    LibM; its two series, with the coefficients of that publication, were
 *    taken as the hypothesis;
 *  - the open choices (which logarithm the table holds and how it is split,
 *    the order of the final additions, which operand is split into a high
 *    and a low part, the two thresholds of the near-1 path) were then
 *    settled by enumerating the candidates against UCRT's results: one
 *    formula matches, and for every variant of it that changes any result
 *    UCRT agrees with the formula and never with the variant (the counts
 *    are in docs/CRT.md).
 * The algorithm is described in src/crt/xx_ms_log2.c and in docs/CRT.md.
 * UCRT uses the same code whether or not the processor has FMA3 (the
 * results are the same with _set_FMA3_enable(0) and (1)), so there is no
 * processor dispatch here either.
 *
 * Test evidence
 * -------------
 * tests/crt/ms_log2_ucrt_test.c links the normal C runtime and compares
 * xx_ms_log2 with the runtime's log2 bit for bit. The run recorded for
 * this version (static UCRT of the Windows SDK, MSVC x64; 3,313,854,944
 * values in all, the numbers per area are in docs/CRT.md) has 0 mismatches
 * in:
 *  - mixed random doubles (random bit patterns of every class, uniform
 *    values in [0, 1), [0.5, 2), around 1 and around both thresholds);
 *  - every special value: +-0, +-inf, quiet and signalling NaNs with
 *    payloads, negative arguments, every power of two from 2^-1074 to
 *    2^1023 and its neighbours, subnormals;
 *  - c/n for every 1 <= c <= n <= 4096 (exhaustive), and c/n for large n;
 *  - 200,000 entropy sums over random histograms (the YARA-X formula).
 * The test is a CTest: xxfc_ms_log2_ucrt in xxfclib (CMake option
 * XXFC_BUILD_CRT_TESTS, MSVC) and xxyara_ms_log2_ucrt in xxyara's tree.
 *
 * Limits
 * ------
 * The equality is established by testing, not by a proof from Microsoft's
 * source. It holds for IEEE-754 binary64 arithmetic evaluated in double
 * precision with round-to-nearest and without contraction of a*b+c into a
 * fused multiply-add; the source file switches contraction off for the
 * compilers it knows. Do not compile it with -ffast-math or with x87
 * extended-precision evaluation. Unlike UCRT, the function sets neither
 * errno nor the floating-point exception flags in a defined way.
 */

#ifndef XXFCLIB_XX_MS_LOG2_H
#define XXFCLIB_XX_MS_LOG2_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief log2 with the exact results of Microsoft UCRT's log2 (x64).
 *
 * Special values, as UCRT returns them: log2(1) = +0, log2(+-0) = -inf,
 * log2(+inf) = +inf, log2(x < 0) and log2(-inf) = -nan(ind)
 * (0xFFF8000000000000), a NaN argument comes back with its sign and payload
 * and the quiet bit set.
 *
 * @param nValue the argument.
 * @return the base-2 logarithm as UCRT rounds it.
 */
XXFC_API double xx_ms_log2(double nValue);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_MS_LOG2_H */
