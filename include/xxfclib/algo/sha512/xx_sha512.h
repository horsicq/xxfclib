/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SHA512_H
#define XX_SHA512_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_SHA224_DIGEST_SIZE 28U
#define XX_SHA384_DIGEST_SIZE 48U
#define XX_SHA512_DIGEST_SIZE 64U
#define XX_SHA512_224_DIGEST_SIZE 28U
#define XX_SHA512_256_DIGEST_SIZE 32U
#define XX_SHA224_BLOCK_SIZE 64U
#define XX_SHA512_BLOCK_SIZE 128U

typedef struct xx_sha224_context {
    uint32_t state[8];
    uint64_t total_size;
    uint8_t buffer[64];
    size_t buffer_size;
    bool initialized;
} xx_sha224_context;

typedef struct xx_sha512_context {
    uint64_t state[8];
    /* A 128-bit byte count, limited to the FIPS 180-4 maximum of < 2^128 bits. */
    uint64_t total_low;
    uint64_t total_high;
    uint8_t buffer[128];
    size_t buffer_size;
    unsigned digest_size;
    bool initialized;
} xx_sha512_context;

typedef xx_sha512_context xx_sha384_context;
typedef xx_sha512_context xx_sha512_224_context;
typedef xx_sha512_context xx_sha512_256_context;

/* Contexts may be copied. All functions reject NULL contexts. Updates reject
 * NULL data for nonempty input and length overflow, without changing context.
 * Final requires at least DIGEST_SIZE bytes, consumes and clears the context
 * on success, and preserves it on error. NULL input is valid for size zero.
 * Context fields are internal state: initialize before using or copying. */
XXFC_API bool xx_sha224_init(xx_sha224_context *context);
XXFC_API bool xx_sha224_update(xx_sha224_context *context, const void *data, size_t size);
XXFC_API bool xx_sha224_final(xx_sha224_context *context, void *digest, size_t digest_size);
XXFC_API bool xx_sha224_memory(const void *data, size_t size, void *digest);

XXFC_API bool xx_sha384_init(xx_sha384_context *context);
XXFC_API bool xx_sha384_update(xx_sha384_context *context, const void *data, size_t size);
XXFC_API bool xx_sha384_final(xx_sha384_context *context, void *digest, size_t digest_size);
XXFC_API bool xx_sha384_memory(const void *data, size_t size, void *digest);

XXFC_API bool xx_sha512_init(xx_sha512_context *context);
XXFC_API bool xx_sha512_update(xx_sha512_context *context, const void *data, size_t size);
XXFC_API bool xx_sha512_final(xx_sha512_context *context, void *digest, size_t digest_size);
XXFC_API bool xx_sha512_memory(const void *data, size_t size, void *digest);

XXFC_API bool xx_sha512_224_init(xx_sha512_224_context *context);
XXFC_API bool xx_sha512_224_update(xx_sha512_224_context *context, const void *data, size_t size);
XXFC_API bool xx_sha512_224_final(xx_sha512_224_context *context, void *digest, size_t digest_size);
XXFC_API bool xx_sha512_224_memory(const void *data, size_t size, void *digest);

XXFC_API bool xx_sha512_256_init(xx_sha512_256_context *context);
XXFC_API bool xx_sha512_256_update(xx_sha512_256_context *context, const void *data, size_t size);
XXFC_API bool xx_sha512_256_final(xx_sha512_256_context *context, void *digest, size_t digest_size);
XXFC_API bool xx_sha512_256_memory(const void *data, size_t size, void *digest);

#ifdef __cplusplus
}
#endif
#endif
