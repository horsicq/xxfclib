/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_DSA_H
#define XX_DSA_H
#include "xxfclib/algo/bignum/xx_bignum.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dsa_public_key { xx_bignum p,q,g,y; } xx_dsa_public_key;
/* p=1024..3072 bits; q=160,224,256 bits. Checks divisibility and subgroup
 * membership, not primality. Inputs must be trusted domain parameters. */
XXFC_API bool xx_dsa_public_key_init(xx_dsa_public_key *key,
    const uint8_t *p,size_t ps,const uint8_t *q,size_t qs,const uint8_t *g,size_t gs,const uint8_t *y,size_t ys);
/* params is DER SEQUENCE {p,q,g}; public_key is DER INTEGER y. */
XXFC_API bool xx_dsa_public_key_from_der(xx_dsa_public_key *key,const uint8_t *params,size_t params_size,
    const uint8_t *public_key,size_t public_key_size);
XXFC_API bool xx_dsa_verify_digest(const xx_dsa_public_key *key,const uint8_t *digest,size_t digest_size,
    const uint8_t *signature,size_t signature_size,bool signature_is_der);
#ifdef __cplusplus
}
#endif
#endif
