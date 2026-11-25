/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_sha_arm_crypto.h"

#ifdef XX_SHA_ARM_CRYPTO
#include "xxfclib/memory/xx_memory.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#define XX_SHA_ARM_TARGET __declspec(noinline)
#elif defined(__clang__)
#include <arm_neon.h>
#define XX_SHA_ARM_TARGET __attribute__((target("crypto"), noinline))
#elif defined(__GNUC__)
#include <arm_neon.h>
#define XX_SHA_ARM_TARGET __attribute__((target("+crypto"), noinline))
#else
#include <arm_neon.h>
#define XX_SHA_ARM_TARGET
#endif

static const uint32_t xx_sha256_arm_constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU,
    0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU,
    0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U,
    0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

static uint32_t xx_sha_arm_rotl1(uint32_t value)
{
    return (value << 1U) | (value >> 31U);
}

static uint32_t xx_sha_arm_rotr(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static void xx_sha1_arm_schedule(const uint8_t *block, uint32_t words[80])
{
    unsigned index;
    for (index = 0U; index < 16U; ++index) {
        words[index] = ((uint32_t)block[index * 4U] << 24U) | ((uint32_t)block[index * 4U + 1U] << 16U) | ((uint32_t)block[index * 4U + 2U] << 8U) |
                       (uint32_t)block[index * 4U + 3U];
    }
    for (index = 16U; index < 80U; ++index) {
        words[index] = xx_sha_arm_rotl1(words[index - 3U] ^ words[index - 8U] ^ words[index - 14U] ^ words[index - 16U]);
    }
}

static void xx_sha256_arm_schedule(const uint8_t *block, uint32_t words[64])
{
    unsigned index;
    for (index = 0U; index < 16U; ++index) {
        words[index] = ((uint32_t)block[index * 4U] << 24U) | ((uint32_t)block[index * 4U + 1U] << 16U) | ((uint32_t)block[index * 4U + 2U] << 8U) |
                       (uint32_t)block[index * 4U + 3U];
    }
    for (index = 16U; index < 64U; ++index) {
        uint32_t previous15 = words[index - 15U];
        uint32_t previous2 = words[index - 2U];
        uint32_t sigma0 = xx_sha_arm_rotr(previous15, 7U) ^ xx_sha_arm_rotr(previous15, 18U) ^ (previous15 >> 3U);
        uint32_t sigma1 = xx_sha_arm_rotr(previous2, 17U) ^ xx_sha_arm_rotr(previous2, 19U) ^ (previous2 >> 10U);
        words[index] = words[index - 16U] + sigma0 + words[index - 7U] + sigma1;
    }
}

XX_SHA_ARM_TARGET
void xx_sha1_blocks_arm_crypto(uint32_t state[5], const uint8_t *blocks, size_t block_count)
{
    uint32_t words[80];
    while (block_count-- != 0U) {
        uint32x4_t abcd = vld1q_u32(state);
        const uint32x4_t saved_abcd = abcd;
        uint32_t e = state[4];
        const uint32_t saved_e = e;
        unsigned index;

        xx_sha1_arm_schedule(blocks, words);
        for (index = 0U; index < 80U; index += 4U) {
            uint32x4_t message = vld1q_u32(words + index);
            uint32_t next_e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
            if (index < 20U) {
                message = vaddq_u32(message, vdupq_n_u32(0x5a827999U));
                abcd = vsha1cq_u32(abcd, e, message);
            } else if (index < 40U) {
                message = vaddq_u32(message, vdupq_n_u32(0x6ed9eba1U));
                abcd = vsha1pq_u32(abcd, e, message);
            } else if (index < 60U) {
                message = vaddq_u32(message, vdupq_n_u32(0x8f1bbcdcU));
                abcd = vsha1mq_u32(abcd, e, message);
            } else {
                message = vaddq_u32(message, vdupq_n_u32(0xca62c1d6U));
                abcd = vsha1pq_u32(abcd, e, message);
            }
            e = next_e;
        }
        abcd = vaddq_u32(abcd, saved_abcd);
        vst1q_u32(state, abcd);
        state[4] = e + saved_e;
        blocks += 64U;
    }
    xx_mem_zero(words, sizeof(words));
}

XX_SHA_ARM_TARGET
void xx_sha256_blocks_arm_crypto(uint32_t state[8], const uint8_t *blocks, size_t block_count)
{
    uint32_t words[64];
    while (block_count-- != 0U) {
        uint32x4_t state0 = vld1q_u32(state);
        uint32x4_t state1 = vld1q_u32(state + 4U);
        const uint32x4_t saved_state0 = state0;
        const uint32x4_t saved_state1 = state1;
        unsigned index;

        xx_sha256_arm_schedule(blocks, words);
        for (index = 0U; index < 64U; index += 4U) {
            uint32x4_t message = vaddq_u32(vld1q_u32(words + index), vld1q_u32(xx_sha256_arm_constants + index));
            uint32x4_t previous_state0 = state0;
            state0 = vsha256hq_u32(state0, state1, message);
            state1 = vsha256h2q_u32(state1, previous_state0, message);
        }
        state0 = vaddq_u32(state0, saved_state0);
        state1 = vaddq_u32(state1, saved_state1);
        vst1q_u32(state, state0);
        vst1q_u32(state + 4U, state1);
        blocks += 64U;
    }
    xx_mem_zero(words, sizeof(words));
}
#endif /* XX_SHA_ARM_CRYPTO */
