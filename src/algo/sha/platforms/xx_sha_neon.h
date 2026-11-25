/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SHA_NEON_H
#define XX_SHA_NEON_H

#include <stddef.h>
#include <stdint.h>

#if ((defined(_M_ARM64) && !defined(_M_ARM64EC)) || (defined(__aarch64__) && !defined(_M_ARM64EC)))
#define XX_SHA_NEON 1

/* Baseline Advanced SIMD computes four message-schedule words together; the
 * compression rounds remain scalar. Callers gate entry with xx_is_neon_enabled(). */
void xx_sha1_blocks_neon(uint32_t state[5], const uint8_t *blocks, size_t block_count);
void xx_sha256_blocks_neon(uint32_t state[8], const uint8_t *blocks, size_t block_count);
#endif

#endif /* XX_SHA_NEON_H */
