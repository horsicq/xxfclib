/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_sha_neon.h"

#ifdef XX_SHA_NEON
#include "xxfclib/memory/xx_memory.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#define XX_SHA_NEON_NOINLINE __declspec(noinline)
#else
#include <arm_neon.h>
#define XX_SHA_NEON_NOINLINE __attribute__((noinline))
#endif

static const uint32_t xx_sha256_neon_constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU,
    0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU,
    0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U,
    0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

static uint32_t xx_sha_neon_rotl(uint32_t value, unsigned bits)
{
    return (value << bits) | (value >> (32U - bits));
}

static uint32_t xx_sha_neon_rotr(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static uint32x4_t xx_sha_neon_rol1q(uint32x4_t value)
{
    return vorrq_u32(vshlq_n_u32(value, 1), vshrq_n_u32(value, 31));
}

static uint32x4_t xx_sha256_neon_sigma0(uint32x4_t value)
{
    uint32x4_t rotate7 = vorrq_u32(vshrq_n_u32(value, 7), vshlq_n_u32(value, 25));
    uint32x4_t rotate18 = vorrq_u32(vshrq_n_u32(value, 18), vshlq_n_u32(value, 14));
    return veorq_u32(veorq_u32(rotate7, rotate18), vshrq_n_u32(value, 3));
}

static uint32x4_t xx_sha256_neon_sigma1(uint32x4_t value)
{
    uint32x4_t rotate17 = vorrq_u32(vshrq_n_u32(value, 17), vshlq_n_u32(value, 15));
    uint32x4_t rotate19 = vorrq_u32(vshrq_n_u32(value, 19), vshlq_n_u32(value, 13));
    return veorq_u32(veorq_u32(rotate17, rotate19), vshrq_n_u32(value, 10));
}

static void xx_sha1_schedule_neon(const uint8_t *block, uint32_t words[80])
{
    const uint32x4_t zero = vdupq_n_u32(0U);
    unsigned index;
    for (index = 0U; index < 16U; index += 4U) {
        uint8x16_t bytes = vrev32q_u8(vld1q_u8(block + (size_t)index * 4U));
        vst1q_u32(words + index, vreinterpretq_u32_u8(bytes));
    }
    for (index = 16U; index < 80U; index += 4U) {
        uint32x4_t minus16 = vld1q_u32(words + index - 16U);
        uint32x4_t minus12 = vld1q_u32(words + index - 12U);
        uint32x4_t minus8 = vld1q_u32(words + index - 8U);
        uint32x4_t minus4 = vld1q_u32(words + index - 4U);
        uint32x4_t minus14 = vextq_u32(minus16, minus12, 2);
        uint32x4_t value = veorq_u32(veorq_u32(minus16, minus14), veorq_u32(minus8, vextq_u32(minus4, zero, 1)));
        value = xx_sha_neon_rol1q(value);
        value = veorq_u32(value, vextq_u32(zero, xx_sha_neon_rol1q(value), 1));
        vst1q_u32(words + index, value);
    }
}

static void xx_sha256_schedule_neon(const uint8_t *block, uint32_t words[64])
{
    const uint32x4_t zero = vdupq_n_u32(0U);
    unsigned index;
    for (index = 0U; index < 16U; index += 4U) {
        uint8x16_t bytes = vrev32q_u8(vld1q_u8(block + (size_t)index * 4U));
        vst1q_u32(words + index, vreinterpretq_u32_u8(bytes));
    }
    for (index = 16U; index < 64U; index += 4U) {
        uint32x4_t minus16 = vld1q_u32(words + index - 16U);
        uint32x4_t minus12 = vld1q_u32(words + index - 12U);
        uint32x4_t minus8 = vld1q_u32(words + index - 8U);
        uint32x4_t minus4 = vld1q_u32(words + index - 4U);
        uint32x4_t minus15 = vextq_u32(minus16, minus12, 1);
        uint32x4_t minus7 = vextq_u32(minus8, minus4, 1);
        uint32x4_t value = vaddq_u32(vaddq_u32(minus16, xx_sha256_neon_sigma0(minus15)), minus7);
        value = vaddq_u32(value, xx_sha256_neon_sigma1(vextq_u32(minus4, zero, 2)));
        value = vaddq_u32(value, xx_sha256_neon_sigma1(vextq_u32(zero, value, 2)));
        vst1q_u32(words + index, value);
    }
}

static void xx_sha1_rounds_neon(uint32_t state[5], const uint32_t words[80])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    unsigned index;
    for (index = 0U; index < 80U; ++index) {
        uint32_t function, constant, next;
        if (index < 20U) {
            function = d ^ (b & (c ^ d));
            constant = 0x5a827999U;
        } else if (index < 40U) {
            function = b ^ c ^ d;
            constant = 0x6ed9eba1U;
        } else if (index < 60U) {
            function = (b & c) | (d & (b | c));
            constant = 0x8f1bbcdcU;
        } else {
            function = b ^ c ^ d;
            constant = 0xca62c1d6U;
        }
        next = xx_sha_neon_rotl(a, 5U) + function + e + constant + words[index];
        e = d;
        d = c;
        c = xx_sha_neon_rotl(b, 30U);
        b = a;
        a = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void xx_sha256_rounds_neon(uint32_t state[8], const uint32_t words[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    unsigned index;
    for (index = 0U; index < 64U; ++index) {
        uint32_t big1 = xx_sha_neon_rotr(e, 6U) ^ xx_sha_neon_rotr(e, 11U) ^ xx_sha_neon_rotr(e, 25U);
        uint32_t choose = g ^ (e & (f ^ g));
        uint32_t temporary1 = h + big1 + choose + xx_sha256_neon_constants[index] + words[index];
        uint32_t big0 = xx_sha_neon_rotr(a, 2U) ^ xx_sha_neon_rotr(a, 13U) ^ xx_sha_neon_rotr(a, 22U);
        uint32_t majority = (a & b) | (c & (a | b));
        uint32_t temporary2 = big0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

XX_SHA_NEON_NOINLINE
void xx_sha1_blocks_neon(uint32_t state[5], const uint8_t *blocks, size_t block_count)
{
    uint32_t words[80];
    while (block_count-- != 0U) {
        xx_sha1_schedule_neon(blocks, words);
        xx_sha1_rounds_neon(state, words);
        blocks += 64U;
    }
    xx_mem_zero(words, sizeof(words));
}

XX_SHA_NEON_NOINLINE
void xx_sha256_blocks_neon(uint32_t state[8], const uint8_t *blocks, size_t block_count)
{
    uint32_t words[64];
    while (block_count-- != 0U) {
        xx_sha256_schedule_neon(blocks, words);
        xx_sha256_rounds_neon(state, words);
        blocks += 64U;
    }
    xx_mem_zero(words, sizeof(words));
}
#endif /* XX_SHA_NEON */
