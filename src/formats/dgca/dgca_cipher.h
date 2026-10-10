/* Independently implemented interoperability primitives. SPDX-License-Identifier: MIT. */
#ifndef DGCA_CIPHER_H
#define DGCA_CIPHER_H
#include <stddef.h>
#include <stdint.h>
#ifdef DG_CIPHER_SHARED
#define DG_CIPHER_API __declspec(dllexport)
#else
#define DG_CIPHER_API
#endif
typedef struct dg_sha512 {
    uint64_t words[8], length;
    unsigned char buffer[128];
    size_t used;
} dg_sha512;
typedef struct dg_cipher {
    uint32_t mt[624];
    size_t mt_pos;
    dg_sha512 hash;
    unsigned char feedback[128], digest[64], previous, position, count;
} dg_cipher;
/* SHA-512 as specified in FIPS 180-4; output uses the standard byte order. */
DG_CIPHER_API void dg_sha512_digest(const void *, size_t, unsigned char[64]);
/* Input key bytes and seed context are supplied explicitly by the container. */
DG_CIPHER_API void dg_cipher_init(dg_cipher *, const void *, size_t, const void *, size_t);
DG_CIPHER_API void dg_cipher_decrypt(dg_cipher *, unsigned char *, size_t);
DG_CIPHER_API size_t dg_cipher_state_size(void);
#endif
