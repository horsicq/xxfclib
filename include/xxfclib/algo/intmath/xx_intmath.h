/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_intmath.h
 * @brief Integer mathematics: GCD, extended Euclidean algorithm,
 * modular inverse, and division by invariant multiplication.
 */

#ifndef XX_INTMATH_H
#define XX_INTMATH_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* --- 1. GCD, Extended Euclid, and Modular Inverse                      --- */
/* ========================================================================= */

/** Greatest common divisor of two unsigned 64-bit integers. */
XXFC_API uint64_t xx_gcd_u64(uint64_t a, uint64_t b);

/**
 * Extended Euclidean algorithm: computes g = gcd(|a|, |b|) and coefficients
 * x, y such that a * x + b * y = g. Returns g. Every int64 input is accepted,
 * INT64_MIN included. The one result that does not fit is g = 2^63 (both
 * inputs in {0, INT64_MIN}, not both 0): it is returned as INT64_MIN and the
 * identity then holds modulo 2^64.
 */
XXFC_API int64_t xx_ext_euclid_i64(int64_t a, int64_t b, int64_t *x, int64_t *y);

/**
 * Modular multiplicative inverse: computes inv such that (a * inv) % m == 1.
 * Returns true if inverse exists (i.e. gcd(a, m) == 1 and m > 1), false otherwise.
 * Works for every 64-bit modulus (also above INT64_MAX); inv is in [1, m).
 */
XXFC_API bool xx_mod_inverse_u64(uint64_t a, uint64_t m, uint64_t *inv);

/* ========================================================================= */
/* --- 2. Division by Invariant Multiplication                           --- */
/* ========================================================================= */

typedef struct xx_magic_u32 {
    uint32_t magic;
    uint32_t shift;
    bool add_flag;
} xx_magic_u32;

typedef struct xx_magic_s32 {
    int32_t magic;
    uint32_t shift;
} xx_magic_s32;

typedef struct xx_magic_u64 {
    uint64_t magic;
    uint32_t shift;
    bool add_flag;
} xx_magic_u64;

typedef struct xx_magic_s64 {
    int64_t magic;
    uint32_t shift;
} xx_magic_s64;

/** Compute magic number and shift for unsigned 32-bit division by constant d >= 1. */
XXFC_API bool xx_magic_compute_u32(uint32_t d, xx_magic_u32 *out);

/** Perform division n / d using precomputed magic parameters. */
XXFC_API uint32_t xx_magic_divide_u32(uint32_t n, const xx_magic_u32 *m);

/** Compute magic number and shift for signed 32-bit division by constant d (d != 0, 1, -1;
 *  INT32_MIN is allowed). Returns false for d = 0, 1, -1. */
XXFC_API bool xx_magic_compute_s32(int32_t d, xx_magic_s32 *out);

/** Perform signed division n / d using precomputed magic parameters. */
XXFC_API int32_t xx_magic_divide_s32(int32_t n, int32_t d, const xx_magic_s32 *m);

/** Compute magic number and shift for unsigned 64-bit division by constant d >= 1. */
XXFC_API bool xx_magic_compute_u64(uint64_t d, xx_magic_u64 *out);

/** Perform unsigned 64-bit division n / d using precomputed magic parameters. */
XXFC_API uint64_t xx_magic_divide_u64(uint64_t n, const xx_magic_u64 *m);

/** Compute magic number and shift for signed 64-bit division by constant d (d != 0, 1, -1;
 *  INT64_MIN is allowed). Returns false for d = 0, 1, -1. */
XXFC_API bool xx_magic_compute_s64(int64_t d, xx_magic_s64 *out);

/** Perform signed 64-bit division n / d using precomputed magic parameters. */
XXFC_API int64_t xx_magic_divide_s64(int64_t n, int64_t d, const xx_magic_s64 *m);

#ifdef __cplusplus
}
#endif

#endif /* XX_INTMATH_H */
