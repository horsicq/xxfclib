/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Norton Secret Stuff's DOS extractor uses the four bytes at offset 4 in
 * MD5(password) as a Blowfish key.  Its 32-bit block words are read and
 * written little-endian; each member uses the eight bytes at true header
 * offsets 38..45 as its CBC IV.  The first encrypted block must decrypt to
 * SYMANTEC.
 *
 * The ciphertext and verifier layout below was checked against the original
 * 16-bit SFX code, independently of any third-party unpacker.
 */
#ifndef XX_SFX_NSS_CRYPTO_PRIVATE_H
#define XX_SFX_NSS_CRYPTO_PRIVATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../catsystem_kif/xx_kif_crypto_private.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/rt/xx_rt.h"

typedef ki_blowfish nss_blowfish;

static uint32_t nss_crypto_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void nss_crypto_put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8U);
    p[2] = (uint8_t)(v >> 16U);
    p[3] = (uint8_t)(v >> 24U);
}

static bool nss_crypto_init_key(nss_blowfish *cipher,
                                const uint8_t key[4]) {
    return cipher && key && ki_bf_init(cipher, key, 4U);
}

static bool nss_crypto_init_password_bytes(nss_blowfish *cipher,
                                           const uint8_t *password,
                                           size_t length) {
    uint8_t digest[XX_MD5_DIGEST_SIZE];
    static const uint8_t empty = 0U;
    bool ok;
    if (!cipher || (!password && length != 0U)) return false;
    /* The original UI accepts at most 50 one-byte characters.  Allow a
     * modestly longer caller-provided value without an unbounded hash. */
    if (length > 1024U) return false;
    ok = xx_md5_memory(password ? password : &empty, length, digest) &&
         nss_crypto_init_key(cipher, digest + 4U);
    xx_rt_memset(digest, 0, sizeof(digest));
    return ok;
}

static bool nss_crypto_init_password(nss_blowfish *cipher,
                                     const char *password) {
    return password && nss_crypto_init_password_bytes(
        cipher, (const uint8_t *)password, xx_rt_strlen(password));
}

/* out may equal ciphertext for in-place decoding.  The verifier is checked
 * before any output is written, so a wrong key never emits named plaintext. */
static bool nss_crypto_decrypt_member(const nss_blowfish *cipher,
                                      const uint8_t iv[8],
                                      const uint8_t *ciphertext,
                                      size_t ciphertext_size,
                                      uint8_t *out, size_t out_capacity,
                                      size_t *out_size) {
    static const uint8_t verifier[8] = {
        'S', 'Y', 'M', 'A', 'N', 'T', 'E', 'C'
    };
    uint8_t previous[8], current[8], plain[8];
    uint32_t left, right;
    size_t at, j;
    if (out_size) *out_size = 0U;
    if (!cipher || !iv || !ciphertext || !out_size ||
        ciphertext_size < 8U || (ciphertext_size & 7U) != 0U ||
        (!out && ciphertext_size > 8U) ||
        out_capacity < ciphertext_size - 8U) return false;
    xx_rt_memcpy(previous, iv, sizeof(previous));
    for (at = 0U; at < ciphertext_size; at += 8U) {
        xx_rt_memcpy(current, ciphertext + at, sizeof(current));
        left = nss_crypto_u32le(current);
        right = nss_crypto_u32le(current + 4U);
        ki_bf_decrypt(cipher, &left, &right);
        nss_crypto_put_u32le(plain, left);
        nss_crypto_put_u32le(plain + 4U, right);
        for (j = 0U; j < 8U; ++j) plain[j] ^= previous[j];
        if (at == 0U) {
            if (xx_rt_memcmp(plain, verifier, sizeof(verifier)) != 0) {
                xx_rt_memset(previous, 0, sizeof(previous));
                xx_rt_memset(current, 0, sizeof(current));
                xx_rt_memset(plain, 0, sizeof(plain));
                return false;
            }
        } else {
            xx_rt_memcpy(out + at - 8U, plain, sizeof(plain));
        }
        xx_rt_memcpy(previous, current, sizeof(previous));
    }
    *out_size = ciphertext_size - 8U;
    xx_rt_memset(previous, 0, sizeof(previous));
    xx_rt_memset(current, 0, sizeof(current));
    xx_rt_memset(plain, 0, sizeof(plain));
    return true;
}

static void nss_crypto_cleanup(nss_blowfish *cipher) {
    if (cipher) xx_rt_memset(cipher, 0, sizeof(*cipher));
}

#endif
