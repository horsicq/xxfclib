/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_BIGNUM_H
#define XX_BIGNUM_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed caller-owned capacity includes full products of 4096-bit operands
 * and modular arithmetic for 8192-bit operands (Montgomery uses wide scratch).
 * Zero has used == 0. Limbs are little-endian base 2^32. Callers must initialize
 * values before use and must not forge noncanonical used/limb combinations.
 * Functions reject invalid values; unused limbs have no numerical meaning.
 * Operations are variable-time and intended for public-key verification only.
 * Every bool operation leaves its output untouched on failure and allows an
 * output to alias an input, except divmod rejects quotient == remainder.
 */
#define XX_BIGNUM_MAX_LIMBS 256U
#define XX_BIGNUM_MAX_BITS (XX_BIGNUM_MAX_LIMBS * 32U)
#define XX_BIGNUM_MAX_BYTES (XX_BIGNUM_MAX_LIMBS * 4U)

typedef struct xx_bignum {
    size_t used;
    uint32_t limbs[XX_BIGNUM_MAX_LIMBS];
} xx_bignum;

typedef struct xx_bignum_mont_context {
    xx_bignum modulus;
    xx_bignum rr;  /* R squared modulo modulus, in ordinary representation. */
    xx_bignum one; /* R modulo modulus: Montgomery representation of 1. */
    size_t limbs;
    uint32_t n0;
    bool initialized;
} xx_bignum_mont_context;

XXFC_API bool xx_bignum_init(xx_bignum *out);
XXFC_API bool xx_bignum_from_u32(xx_bignum *out, uint32_t value);
/* NULL data is valid only for zero size. Leading zero bytes are accepted,
 * but the complete supplied length must fit MAX_BYTES. */
XXFC_API bool xx_bignum_from_be(xx_bignum *out, const uint8_t *data, size_t size);
/* Writes exactly size bytes, zero-padding on the left. NULL/0 accepts zero. */
XXFC_API bool xx_bignum_to_be(const xx_bignum *value, uint8_t *data, size_t size);
/* Invalid input returns 0; compare also returns 0 for invalid inputs. */
XXFC_API size_t xx_bignum_bit_length(const xx_bignum *value);
XXFC_API int xx_bignum_cmp(const xx_bignum *a, const xx_bignum *b);
XXFC_API bool xx_bignum_add(xx_bignum *out, const xx_bignum *a, const xx_bignum *b);
XXFC_API bool xx_bignum_sub(xx_bignum *out, const xx_bignum *a, const xx_bignum *b);
XXFC_API bool xx_bignum_mul(xx_bignum *out, const xx_bignum *a, const xx_bignum *b);
/* Either output may be NULL, but at least one must be supplied. Divisor > 0. */
XXFC_API bool xx_bignum_divmod(xx_bignum *quotient, xx_bignum *remainder, const xx_bignum *a, const xx_bignum *b);
XXFC_API bool xx_bignum_mod(xx_bignum *out, const xx_bignum *a, const xx_bignum *modulus);
XXFC_API bool xx_bignum_modadd(xx_bignum *out, const xx_bignum *a, const xx_bignum *b, const xx_bignum *modulus);
XXFC_API bool xx_bignum_modsub(xx_bignum *out, const xx_bignum *a, const xx_bignum *b, const xx_bignum *modulus);
XXFC_API bool xx_bignum_modmul(xx_bignum *out, const xx_bignum *a, const xx_bignum *b, const xx_bignum *modulus);
/* 0^0 is 1 modulo modulus; modulus must be nonzero. */
XXFC_API bool xx_bignum_modexp(xx_bignum *out, const xx_bignum *base, const xx_bignum *exponent, const xx_bignum *modulus);
/* Works for odd or even moduli > 1; fails when gcd(a, modulus) != 1. */
XXFC_API bool xx_bignum_modinv(xx_bignum *out, const xx_bignum *a, const xx_bignum *modulus);

/* Reusable Montgomery arithmetic for odd moduli > 1, up to MAX_BITS.
 * Import accepts any valid integer. Multiply/export require values < modulus.
 * Values passed to multiply are Montgomery residues; export maps to ordinary
 * representation. Context must be initialized and must not alias any output.
 */
XXFC_API bool xx_bignum_mont_init(xx_bignum_mont_context *context, const xx_bignum *modulus);
XXFC_API bool xx_bignum_mont_import(xx_bignum *out, const xx_bignum *value, const xx_bignum_mont_context *context);
XXFC_API bool xx_bignum_mont_export(xx_bignum *out, const xx_bignum *value, const xx_bignum_mont_context *context);
XXFC_API bool xx_bignum_mont_mul(xx_bignum *out, const xx_bignum *a, const xx_bignum *b, const xx_bignum_mont_context *context);

#ifdef __cplusplus
}
#endif
#endif /* XX_BIGNUM_H */
