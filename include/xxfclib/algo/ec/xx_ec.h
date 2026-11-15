/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_EC_H
#define XX_EC_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_ec_curve {
    XX_EC_P256 = 1,
    XX_EC_P384 = 2
} xx_ec_curve;

/* Coordinates occupy the first xx_ec_field_size(curve) bytes, big endian.
 * Remaining bytes must be zero. Infinity has zero coordinates. */
typedef struct xx_ec_point {
    uint8_t x[48];
    uint8_t y[48];
    bool infinity;
} xx_ec_point;

XXFC_API size_t xx_ec_field_size(xx_ec_curve curve);
/* All field buffers hold field_size bytes; inputs must be smaller than p.
 * Output may alias either input. Failure leaves output unchanged. */
XXFC_API bool xx_ec_field_add(xx_ec_curve curve, uint8_t *out, const uint8_t *a, const uint8_t *b);
XXFC_API bool xx_ec_field_sub(xx_ec_curve curve, uint8_t *out, const uint8_t *a, const uint8_t *b);
XXFC_API bool xx_ec_field_mul(xx_ec_curve curve, uint8_t *out, const uint8_t *a, const uint8_t *b);
XXFC_API bool xx_ec_field_inv(xx_ec_curve curve, uint8_t *out, const uint8_t *a);

XXFC_API bool xx_ec_generator(xx_ec_curve curve, xx_ec_point *out);
/* Only canonical, finite compressed (02/03) and uncompressed (04) SEC1
 * encodings are accepted. Hybrid and infinity encodings are rejected. */
XXFC_API bool xx_ec_point_decode(xx_ec_curve curve, xx_ec_point *out, const uint8_t *encoded, size_t size);
/* out_size must be exactly 1 for infinity, 1+field_size for compressed,
 * or 1+2*field_size for uncompressed. */
XXFC_API bool xx_ec_point_encode(xx_ec_curve curve, const xx_ec_point *point, bool compressed, uint8_t *out, size_t out_size);
XXFC_API bool xx_ec_point_add(xx_ec_curve curve, xx_ec_point *out, const xx_ec_point *a, const xx_ec_point *b);
/* A scalar has at most field_size bytes. Zero (including NULL/0) is valid.
 * Scalar multiplication does not reduce the scalar modulo the order. */
XXFC_API bool xx_ec_point_mul(xx_ec_curve curve, xx_ec_point *out, const xx_ec_point *point, const uint8_t *scalar, size_t scalar_size);

/* Verification only: variable-time arithmetic, no private-key operations.
 * Raw signature is fixed-width r || s; DER must be minimal positive INTEGERs.
 * The digest is truncated to the leftmost order bits as specified by SEC1.
 * Finite public keys are validated on-curve and in the prime-order subgroup. */
XXFC_API bool xx_ecdsa_verify_digest(xx_ec_curve curve,
                                    const uint8_t *public_key, size_t public_key_size,
                                    const uint8_t *digest, size_t digest_size,
                                    const uint8_t *signature, size_t signature_size,
                                    bool signature_is_der);

#ifdef __cplusplus
}
#endif
#endif /* XX_EC_H */
