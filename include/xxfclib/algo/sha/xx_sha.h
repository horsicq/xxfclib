/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sha.h
 * @brief SHA-1 and SHA-256 with automatic CPU acceleration.
 *
 * Contexts may be copied to take a snapshot of an unfinished digest. Final
 * consumes and securely clears the context; initialize it again for reuse.
 * Digest bytes use the algorithms' big-endian byte order on every host.
 */
#ifndef XX_SHA_H
#define XX_SHA_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_SHA1_BLOCK_SIZE 64U
#define XX_SHA256_BLOCK_SIZE 64U
#define XX_SHA1_DIGEST_SIZE 20
#define XX_SHA256_DIGEST_SIZE 32

typedef struct xx_sha1_context {
    uint32_t state[5];
    uint64_t total_size;
    uint8_t buffer[XX_SHA1_BLOCK_SIZE];
    size_t buffer_size;
    bool initialized;
} xx_sha1_context;

typedef struct xx_sha256_context {
    uint32_t state[8];
    uint64_t total_size;
    uint8_t buffer[XX_SHA256_BLOCK_SIZE];
    size_t buffer_size;
    bool initialized;
} xx_sha256_context;

/** Begin a digest. Return false for a NULL context. */
XXFC_API bool xx_sha1_init(xx_sha1_context *context);
XXFC_API bool xx_sha256_init(xx_sha256_context *context);

/** Feed bytes to an initialized context. A NULL pointer is valid for size 0. */
XXFC_API void xx_sha1_update(xx_sha1_context *context, const void *data, size_t size);
XXFC_API void xx_sha256_update(xx_sha256_context *context, const void *data, size_t size);

/** Finish a digest. Invalid output leaves the unfinished context intact. */
XXFC_API bool xx_sha1_final(xx_sha1_context *context, void *digest, size_t digest_size);
XXFC_API bool xx_sha256_final(xx_sha256_context *context, void *digest, size_t digest_size);

/** Digest a buffer into 20 or 32 bytes. NULL input with size 0 is valid. */
XXFC_API bool xx_sha1_memory(const void *data, size_t size, void *digest);
XXFC_API bool xx_sha256_memory(const void *data, size_t size, void *digest);

#ifdef __cplusplus
}
#endif
#endif /* XX_SHA_H */
