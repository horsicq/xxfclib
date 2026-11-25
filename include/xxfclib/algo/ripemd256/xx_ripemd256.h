/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_RIPEMD256_H
#define XX_RIPEMD256_H

#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_RIPEMD256_BLOCK_SIZE 64U
#define XX_RIPEMD256_DIGEST_SIZE 32U

/** Copyable unfinished digest. Final consumes and clears the context.
 * Input/output ranges must not overlap the context. */
typedef struct xx_ripemd256_context {
    uint32_t state[8];
    uint64_t length;
    uint8_t buffer[XX_RIPEMD256_BLOCK_SIZE];
    size_t buffered;
    bool initialized;
} xx_ripemd256_context;

XXFC_API bool xx_ripemd256_init(xx_ripemd256_context *context);
/** NULL input is valid for size zero. Invalid calls leave the context intact. */
XXFC_API bool xx_ripemd256_update(xx_ripemd256_context *context, const void *input, size_t size);
/** Write 32 bytes. Invalid/short output leaves the context and output intact. */
XXFC_API bool xx_ripemd256_final(xx_ripemd256_context *context, void *digest, size_t digest_size);
/** One-shot digest. Input and digest may overlap; NULL empty input is valid. */
XXFC_API bool xx_ripemd256_memory(const void *input, size_t size, void *digest);

#ifdef __cplusplus
}
#endif
#endif
