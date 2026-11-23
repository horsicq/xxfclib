/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_nchash.h
 * @brief Non-cryptographic and extended hash suite (FNV, DJB2, SDBM, ELF, PJW,
 *        BKDR, AP, DEK, JS, RS, One-at-a-time, MurmurHash3, xxHash, SipHash-2-4,
 *        BLAKE2b-512, and BLAKE2s-256).
 */

#ifndef XX_NCHASH_H
#define XX_NCHASH_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_nchash_type_e {
    XX_NCHASH_FNV1_32,
    XX_NCHASH_FNV1A_32,
    XX_NCHASH_FNV1_64,
    XX_NCHASH_FNV1A_64,
    XX_NCHASH_DJB2,
    XX_NCHASH_SDBM,
    XX_NCHASH_ELF,
    XX_NCHASH_PJW,
    XX_NCHASH_BKDR,
    XX_NCHASH_AP,
    XX_NCHASH_DEK,
    XX_NCHASH_JS,
    XX_NCHASH_RS,
    XX_NCHASH_OAT,
    XX_NCHASH_MURMUR3_32,
    XX_NCHASH_MURMUR3_128,
    XX_NCHASH_XXHASH32,
    XX_NCHASH_XXHASH64,
    XX_NCHASH_SIPHASH64,
    XX_NCHASH_SIPHASH128,
    XX_NCHASH_BLAKE2B_512,
    XX_NCHASH_BLAKE2S_256,
    XX_NCHASH_COUNT
} xx_nchash_type_t;

/* --- Streaming Contexts --- */

typedef struct xx_nchash_fnv1_32_ctx { uint32_t state; } xx_nchash_fnv1_32_ctx;
typedef struct xx_nchash_fnv1a_32_ctx { uint32_t state; } xx_nchash_fnv1a_32_ctx;
typedef struct xx_nchash_fnv1_64_ctx { uint64_t state; } xx_nchash_fnv1_64_ctx;
typedef struct xx_nchash_fnv1a_64_ctx { uint64_t state; } xx_nchash_fnv1a_64_ctx;
typedef struct xx_nchash_djb2_ctx { uint32_t state; } xx_nchash_djb2_ctx;
typedef struct xx_nchash_sdbm_ctx { uint32_t state; } xx_nchash_sdbm_ctx;
typedef struct xx_nchash_elf_ctx { uint32_t state; } xx_nchash_elf_ctx;
typedef struct xx_nchash_pjw_ctx { uint32_t state; } xx_nchash_pjw_ctx;
typedef struct xx_nchash_bkdr_ctx { uint32_t state; } xx_nchash_bkdr_ctx;
typedef struct xx_nchash_ap_ctx { uint32_t state; size_t index; } xx_nchash_ap_ctx;
typedef struct xx_nchash_dek_ctx { uint32_t state; size_t length; } xx_nchash_dek_ctx;
typedef struct xx_nchash_js_ctx { uint32_t state; } xx_nchash_js_ctx;
typedef struct xx_nchash_rs_ctx { uint32_t state; uint32_t a; } xx_nchash_rs_ctx;
typedef struct xx_nchash_oat_ctx { uint32_t state; } xx_nchash_oat_ctx;

typedef struct xx_nchash_murmur3_32_ctx {
    uint32_t h;
    uint32_t total_len;
    uint8_t buffer[4];
    size_t buffered;
} xx_nchash_murmur3_32_ctx;

typedef struct xx_nchash_murmur3_128_ctx {
    uint64_t h1, h2;
    uint64_t total_len;
    uint8_t buffer[16];
    size_t buffered;
} xx_nchash_murmur3_128_ctx;

typedef struct xx_nchash_xxhash32_ctx {
    uint32_t v1, v2, v3, v4;
    uint64_t total_len;
    uint8_t buffer[16];
    size_t buffered;
    uint32_t seed;
} xx_nchash_xxhash32_ctx;

typedef struct xx_nchash_xxhash64_ctx {
    uint64_t v1, v2, v3, v4;
    uint64_t total_len;
    uint8_t buffer[32];
    size_t buffered;
    uint64_t seed;
} xx_nchash_xxhash64_ctx;

typedef struct xx_nchash_siphash_ctx {
    uint64_t v0, v1, v2, v3;
    uint64_t total_len;
    uint8_t buffer[8];
    size_t buffered;
    bool is128;
} xx_nchash_siphash_ctx;

typedef struct xx_nchash_blake2b_ctx {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t buf[128];
    size_t buflen;
} xx_nchash_blake2b_ctx;

typedef struct xx_nchash_blake2s_ctx {
    uint32_t h[8];
    uint32_t t[2];
    uint32_t f[2];
    uint8_t buf[64];
    size_t buflen;
} xx_nchash_blake2s_ctx;

/* Unified generic context */
typedef struct xx_nchash_context {
    xx_nchash_type_t type;
    uint64_t total_size;
    bool initialized;
    union {
        xx_nchash_fnv1_32_ctx fnv1_32;
        xx_nchash_fnv1a_32_ctx fnv1a_32;
        xx_nchash_fnv1_64_ctx fnv1_64;
        xx_nchash_fnv1a_64_ctx fnv1a_64;
        xx_nchash_djb2_ctx djb2;
        xx_nchash_sdbm_ctx sdbm;
        xx_nchash_elf_ctx elf;
        xx_nchash_pjw_ctx pjw;
        xx_nchash_bkdr_ctx bkdr;
        xx_nchash_ap_ctx ap;
        xx_nchash_dek_ctx dek;
        xx_nchash_js_ctx js;
        xx_nchash_rs_ctx rs;
        xx_nchash_oat_ctx oat;
        xx_nchash_murmur3_32_ctx murmur3_32;
        xx_nchash_murmur3_128_ctx murmur3_128;
        xx_nchash_xxhash32_ctx xxhash32;
        xx_nchash_xxhash64_ctx xxhash64;
        xx_nchash_siphash_ctx siphash;
        xx_nchash_blake2b_ctx blake2b;
        xx_nchash_blake2s_ctx blake2s;
    } u;
} xx_nchash_context;

/* --- Unified API --- */

XXFC_API bool xx_nchash_context_init(xx_nchash_context *ctx, xx_nchash_type_t type);
XXFC_API void xx_nchash_context_set_key(xx_nchash_context *ctx, const uint8_t key[16]);
XXFC_API void xx_nchash_context_set_seed(xx_nchash_context *ctx, uint64_t seed);
XXFC_API void xx_nchash_context_update(xx_nchash_context *ctx, const void *data, size_t size);
XXFC_API bool xx_nchash_context_final(xx_nchash_context *ctx, void *digest, size_t digest_size);

XXFC_API const char *xx_nchash_type_to_string(xx_nchash_type_t type);
XXFC_API xx_nchash_type_t xx_nchash_string_to_type(const char *name);
XXFC_API size_t xx_nchash_digest_size(xx_nchash_type_t type);

/* --- One-Shot Functions --- */

XXFC_API uint32_t xx_nchash_fnv1_32(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_fnv1a_32(const void *data, size_t size);
XXFC_API uint64_t xx_nchash_fnv1_64(const void *data, size_t size);
XXFC_API uint64_t xx_nchash_fnv1a_64(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_djb2(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_sdbm(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_elf(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_pjw(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_bkdr(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_ap(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_dek(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_js(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_rs(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_oat(const void *data, size_t size);
XXFC_API uint32_t xx_nchash_murmur3_32(const void *data, size_t size, uint32_t seed);
XXFC_API void xx_nchash_murmur3_128(const void *data, size_t size, uint64_t seed, uint8_t out[16]);
XXFC_API uint32_t xx_nchash_xxhash32(const void *data, size_t size, uint32_t seed);
XXFC_API uint64_t xx_nchash_xxhash64(const void *data, size_t size, uint64_t seed);
XXFC_API uint64_t xx_nchash_siphash64(const void *data, size_t size, const uint8_t key[16]);
XXFC_API void xx_nchash_siphash128(const void *data, size_t size, const uint8_t key[16], uint8_t out[16]);
XXFC_API bool xx_nchash_blake2b_512(const void *data, size_t size, uint8_t digest[64]);
XXFC_API bool xx_nchash_blake2s_256(const void *data, size_t size, uint8_t digest[32]);

/* Device helper */
XXFC_API bool xx_nchash_device(xx_io_device *device, int64_t offset, int64_t size,
                               xx_nchash_type_t type, const uint8_t *key_or_null,
                               uint64_t seed, void *digest, size_t digest_size,
                               xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_NCHASH_H */
