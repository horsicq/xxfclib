/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_aes_arm_crypto.h"

#ifdef XX_AES_ARM_CRYPTO
#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#define XX_AES_ARM_TARGET __declspec(noinline)
#elif defined(__clang__)
#include <arm_neon.h>
#define XX_AES_ARM_TARGET __attribute__((target("crypto"), noinline))
#elif defined(__GNUC__)
#include <arm_neon.h>
#define XX_AES_ARM_TARGET __attribute__((target("+crypto"), noinline))
#else
#include <arm_neon.h>
#define XX_AES_ARM_TARGET
#endif

/* A zero AESE/AESD round key keeps the operation order explicit and lets this
 * backend consume the same unmodified schedule as scalar code and AES-NI. */
XX_AES_ARM_TARGET
void xx_aes_encrypt_block_arm_crypto(const xx_aes_context *context, const uint8_t input[XX_AES_BLOCK_SIZE], uint8_t output[XX_AES_BLOCK_SIZE])
{
    const uint8x16_t zero = vdupq_n_u8(0U);
    uint8x16_t state = veorq_u8(vld1q_u8(input), vld1q_u8(context->round_keys));
    unsigned round;

    for (round = 1U; round < (unsigned)context->rounds; ++round) {
        state = vaesmcq_u8(vaeseq_u8(state, zero));
        state = veorq_u8(state, vld1q_u8(context->round_keys + ((size_t)round * XX_AES_BLOCK_SIZE)));
    }
    state = vaeseq_u8(state, zero);
    state = veorq_u8(state, vld1q_u8(context->round_keys + ((size_t)context->rounds * XX_AES_BLOCK_SIZE)));
    vst1q_u8(output, state);
}

XX_AES_ARM_TARGET
void xx_aes_decrypt_block_arm_crypto(const xx_aes_context *context, const uint8_t input[XX_AES_BLOCK_SIZE], uint8_t output[XX_AES_BLOCK_SIZE])
{
    const uint8x16_t zero = vdupq_n_u8(0U);
    uint8x16_t state = veorq_u8(vld1q_u8(input), vld1q_u8(context->round_keys + ((size_t)context->rounds * XX_AES_BLOCK_SIZE)));
    unsigned round;

    for (round = (unsigned)context->rounds - 1U; round != 0U; --round) {
        state = vaesdq_u8(state, zero);
        state = veorq_u8(state, vld1q_u8(context->round_keys + ((size_t)round * XX_AES_BLOCK_SIZE)));
        state = vaesimcq_u8(state);
    }
    state = vaesdq_u8(state, zero);
    state = veorq_u8(state, vld1q_u8(context->round_keys));
    vst1q_u8(output, state);
}
#endif /* XX_AES_ARM_CRYPTO */
