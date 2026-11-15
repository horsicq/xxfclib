/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PKI_VERIFY_H
#define XX_PKI_VERIFY_H
#include "xxfclib/global/xx_global.h"
#include "xxfclib/algo/md2/xx_md2.h"
#include "xxfclib/algo/md5/xx_md5.h"
#include "xxfclib/algo/sha/xx_sha.h"
#include "xxfclib/algo/sha512/xx_sha512.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_pki_hash {
    XX_PKI_HASH_NONE = 0, XX_PKI_HASH_MD2, XX_PKI_HASH_MD5, XX_PKI_HASH_SHA1, XX_PKI_HASH_SHA224,
    XX_PKI_HASH_SHA256, XX_PKI_HASH_SHA384, XX_PKI_HASH_SHA512
} xx_pki_hash;
/* Copyable caller-owned runtime-selected hash. Final consumes the context.
 * Failed calls preserve it. NULL data is permitted only with size zero. */
typedef struct xx_pki_hash_context {
    xx_pki_hash algorithm;
    union {
        xx_md2_context md2;
        xx_md5_context md5;
        xx_sha1_context sha1;
        xx_sha224_context sha224;
        xx_sha256_context sha256;
        xx_sha512_context sha512;
    } state;
} xx_pki_hash_context;
typedef struct xx_pki_hash_part { const void *data; size_t size; } xx_pki_hash_part;
XXFC_API size_t xx_pki_hash_size(xx_pki_hash alg);
XXFC_API bool xx_pki_hash_init(xx_pki_hash_context *context, xx_pki_hash alg);
XXFC_API bool xx_pki_hash_update(xx_pki_hash_context *context, const void *data, size_t size);
XXFC_API bool xx_pki_hash_final(xx_pki_hash_context *context, uint8_t *out, size_t out_size);
XXFC_API bool xx_pki_hash_memoryv(xx_pki_hash alg, const xx_pki_hash_part *parts, size_t count, uint8_t *out);
/* out must have hash_size(alg) bytes; NULL data is permitted only with size 0. */
XXFC_API bool xx_pki_hash_memory(xx_pki_hash alg, const void *data, size_t size, uint8_t *out);
XXFC_API xx_pki_hash xx_pki_hash_from_oid(const char *dotted_oid);
/* Strict DER SPKI and signature parameters. PSS requires explicit DER parameters.
 * NULL or empty sig_alg_oid selects the SPKI key's operation with PKCS#1 v1.5
 * for RSA, ignoring signature parameters and OID/hash consistency (CMS mode).
 * RSA keys <=8192 bits; DSA p 1024..3072/q 160,224,256; EC P-256/P-384.
 * False includes unsupported algorithms/keys, malformed input and bad signatures.
 * Verification only: timing is not constant; no private keys are supported. */
XXFC_API bool xx_pki_verify_digest(const uint8_t *spki_der, size_t spki_size,
                                   const char *sig_alg_oid,
                                   const uint8_t *sig_alg_params_der, size_t sig_alg_params_size,
                                   xx_pki_hash digest_alg, const uint8_t *digest, size_t digest_size,
                                   const uint8_t *signature, size_t signature_size);
#ifdef __cplusplus
}
#endif
#endif
