/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_RSA_H
#define XX_RSA_H
#include "xxfclib/algo/bignum/xx_bignum.h"
#include "xxfclib/algo/pki/xx_pki_verify.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XX_RSA_MAX_BYTES 1024U
#define XX_RSA_PSS_SALT_AUTO SIZE_MAX
typedef struct xx_rsa_public_key { xx_bignum n, e; } xx_rsa_public_key;
/* Import unsigned big-endian components, or a strict DER PKCS#1 RSAPublicKey. */
XXFC_API bool xx_rsa_public_key_init(xx_rsa_public_key *key, const uint8_t *n, size_t n_size, const uint8_t *e, size_t e_size);
XXFC_API bool xx_rsa_public_key_from_der(xx_rsa_public_key *key, const uint8_t *der, size_t der_size);
/* Matches the upstream rsa crate's canonical DigestInfo (NULL parameters).
 * The optional raw-digest fallback matches YARA-X's new_unprefixed() retry. */
XXFC_API bool xx_rsa_pkcs1_verify_digest(const xx_rsa_public_key *key, xx_pki_hash alg,
    const uint8_t *digest, size_t digest_size, const uint8_t *signature, size_t signature_size, bool allow_unprefixed);
/* salt_size==XX_RSA_PSS_SALT_AUTO derives the salt length from the encoded block. */
XXFC_API bool xx_rsa_pss_verify_digest(const xx_rsa_public_key *key, xx_pki_hash alg, xx_pki_hash mgf_alg,
    size_t salt_size, const uint8_t *digest, size_t digest_size, const uint8_t *signature, size_t signature_size);
/* MGF1 permits an empty seed/output; output and seed must not overlap. */
XXFC_API bool xx_rsa_mgf1(xx_pki_hash alg, const uint8_t *seed, size_t seed_size, uint8_t *out, size_t out_size);
#ifdef __cplusplus
}
#endif
#endif
