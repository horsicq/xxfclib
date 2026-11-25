/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SHA_ARM_CRYPTO_H
#define XX_SHA_ARM_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#if ((defined(_M_ARM64) && !defined(_M_ARM64EC)) || (defined(__aarch64__) && !defined(_M_ARM64EC)))
#define XX_SHA_ARM_CRYPTO 1

/* These entry points contain optional Armv8 SHA instructions. Callers must
 * gate entry with xx_is_arm_crypto_enabled(). */
void xx_sha1_blocks_arm_crypto(uint32_t state[5], const uint8_t *blocks, size_t block_count);
void xx_sha256_blocks_arm_crypto(uint32_t state[8], const uint8_t *blocks, size_t block_count);
#endif

#endif /* XX_SHA_ARM_CRYPTO_H */
