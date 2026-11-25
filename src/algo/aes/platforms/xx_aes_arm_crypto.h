/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_AES_ARM_CRYPTO_H
#define XX_AES_ARM_CRYPTO_H

#include "../xx_aes_internal.h"

#if ((defined(_M_ARM64) && !defined(_M_ARM64EC)) || (defined(__aarch64__) && !defined(_M_ARM64EC)))
#define XX_AES_ARM_CRYPTO 1

/* These entry points contain optional Armv8 crypto instructions. Callers must
 * gate entry with xx_is_arm_crypto_enabled(). */
void xx_aes_encrypt_block_arm_crypto(const xx_aes_context *context, const uint8_t input[XX_AES_BLOCK_SIZE], uint8_t output[XX_AES_BLOCK_SIZE]);
void xx_aes_decrypt_block_arm_crypto(const xx_aes_context *context, const uint8_t input[XX_AES_BLOCK_SIZE], uint8_t output[XX_AES_BLOCK_SIZE]);
#endif

#endif /* XX_AES_ARM_CRYPTO_H */
