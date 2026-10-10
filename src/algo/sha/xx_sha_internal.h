/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SHA_INTERNAL_H
#define XX_SHA_INTERNAL_H

#include "xxfclib/xxfc_defs.h"

/* Complete 64-byte blocks; unaligned byte pointers are supported. These
 * routines update the chaining state without buffering or final padding. */
void xx_sha1_blocks_scalar(uint32_t state[5], const uint8_t *blocks, size_t block_count);
void xx_sha256_blocks_scalar(uint32_t state[8], const uint8_t *blocks, size_t block_count);
void xx_sha1_blocks(uint32_t state[5], const uint8_t *blocks, size_t block_count);
void xx_sha256_blocks(uint32_t state[8], const uint8_t *blocks, size_t block_count);

/* Diagnostics and explicit per-call selection for regression tests. No
 * global override is used, so ordinary callers keep automatic dispatch. */
enum xx_sha_backend {
    XX_SHA_BACKEND_AUTO = 0,
    XX_SHA_BACKEND_SCALAR = 1,
    XX_SHA_BACKEND_SSE2 = 2,
    XX_SHA_BACKEND_SHA_NI = 3
};
unsigned xx_sha_backend_capabilities(void);
int xx_sha_selected_backend(void);
const char *xx_sha_backend_name(int backend);
bool xx_sha1_blocks_backend(uint32_t state[5], const uint8_t *blocks, size_t block_count, int backend);
bool xx_sha256_blocks_backend(uint32_t state[8], const uint8_t *blocks, size_t block_count, int backend);

#endif /* XX_SHA_INTERNAL_H */
