/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/sha/xx_sha.h"
#include "xx_sha_internal.h"
#include "xxfclib/rt/xx_rt.h"

/* SHA is also used by password derivation: preserve explicit clearing even
 * when the compiler can prove that the context or schedule is no longer read. */
static void sha_clear(void *data, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    while (size-- > 0U) *bytes++ = 0U;
}

static uint32_t sha_rotl32(uint32_t value, unsigned bits)
{
    return (uint32_t)((value << bits) | (value >> (32U - bits)));
}

static uint32_t sha_rotr32(uint32_t value, unsigned bits)
{
    return (uint32_t)((value >> bits) | (value << (32U - bits)));
}

/* ----------------------------------------------------------------- SHA-1 -- */

static void sha1_transform(uint32_t *state, const uint8_t *block, uint32_t w[80])
{
    uint32_t a, b, c, d, e;
    unsigned i;

    for (i = 0; i < 16U; ++i) {
        w[i] = ((uint32_t)block[i * 4U] << 24) | ((uint32_t)block[i * 4U + 1U] << 16) | ((uint32_t)block[i * 4U + 2U] << 8) | (uint32_t)block[i * 4U + 3U];
    }
    for (i = 16U; i < 80U; ++i) {
        w[i] = sha_rotl32(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    for (i = 0; i < 80U; ++i) {
        uint32_t f, k;
        if (i < 20U) {
            f = (b & c) | (~b & d);
            k = 0x5a827999U;
        } else if (i < 40U) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1U;
        } else if (i < 60U) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcU;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6U;
        }
        {
            uint32_t temp = sha_rotl32(a, 5U) + f + e + k + w[i];
            e = d;
            d = c;
            c = sha_rotl32(b, 30U);
            b = a;
            a = temp;
        }
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

/* --------------------------------------------------------------- SHA-256 -- */

static const uint32_t SHA256_K[64] = {0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
                                      0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
                                      0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
                                      0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
                                      0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
                                      0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
                                      0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

static void sha256_transform(uint32_t *state, const uint8_t *block, uint32_t w[64])
{
    uint32_t a, b, c, d, e, f, g, h;
    unsigned i;

    for (i = 0; i < 16U; ++i) {
        w[i] = ((uint32_t)block[i * 4U] << 24) | ((uint32_t)block[i * 4U + 1U] << 16) | ((uint32_t)block[i * 4U + 2U] << 8) | (uint32_t)block[i * 4U + 3U];
    }
    for (i = 16U; i < 64U; ++i) {
        uint32_t s0 = sha_rotr32(w[i - 15U], 7U) ^ sha_rotr32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3);
        uint32_t s1 = sha_rotr32(w[i - 2U], 17U) ^ sha_rotr32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    for (i = 0; i < 64U; ++i) {
        uint32_t s1 = sha_rotr32(e, 6U) ^ sha_rotr32(e, 11U) ^ sha_rotr32(e, 25U);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + SHA256_K[i] + w[i];
        uint32_t s0 = sha_rotr32(a, 2U) ^ sha_rotr32(a, 13U) ^ sha_rotr32(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
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

/* --------------------------------------------------------- block kernels -- */

void xx_sha1_blocks_scalar(uint32_t state[5], const uint8_t *blocks, size_t block_count)
{
    uint32_t schedule[80];
    while (block_count-- > 0U) {
        sha1_transform(state, blocks, schedule);
        blocks += XX_SHA1_BLOCK_SIZE;
    }
    sha_clear(schedule, sizeof(schedule));
}

void xx_sha256_blocks_scalar(uint32_t state[8], const uint8_t *blocks, size_t block_count)
{
    uint32_t schedule[64];
    while (block_count-- > 0U) {
        sha256_transform(state, blocks, schedule);
        blocks += XX_SHA256_BLOCK_SIZE;
    }
    sha_clear(schedule, sizeof(schedule));
}

typedef void (*sha_blocks_function)(uint32_t *, const uint8_t *, size_t);

static void sha_update(uint32_t *state, uint64_t *total_size, uint8_t buffer[64], size_t *buffer_size, const uint8_t *input, size_t size, sha_blocks_function blocks)
{
    size_t count;
    if (size == 0U) return;
    *total_size += (uint64_t)size;
    if (*buffer_size != 0U) {
        size_t need = 64U - *buffer_size;
        size_t copy = size < need ? size : need;
        xx_rt_memcpy(buffer + *buffer_size, input, copy);
        *buffer_size += copy;
        input += copy;
        size -= copy;
        if (*buffer_size < 64U) return;
        blocks(state, buffer, 1U);
        *buffer_size = 0U;
    }
    count = size / 64U;
    if (count != 0U) {
        blocks(state, input, count);
        input += count * 64U;
        size -= count * 64U;
    }
    if (size != 0U) {
        xx_rt_memcpy(buffer, input, size);
        *buffer_size = size;
    }
}

static void sha_final(uint32_t *state, unsigned words, uint64_t total_size, uint8_t buffer[64], size_t buffer_size, uint8_t *digest, sha_blocks_function blocks)
{
    uint64_t bits = total_size * 8U;
    unsigned i;
    buffer[buffer_size++] = 0x80U;
    if (buffer_size > 56U) {
        xx_rt_memset(buffer + buffer_size, 0, 64U - buffer_size);
        blocks(state, buffer, 1U);
        buffer_size = 0U;
    }
    xx_rt_memset(buffer + buffer_size, 0, 56U - buffer_size);
    for (i = 0U; i < 8U; ++i) buffer[63U - i] = (uint8_t)(bits >> (8U * i));
    blocks(state, buffer, 1U);
    for (i = 0U; i < words; ++i) {
        digest[i * 4U] = (uint8_t)(state[i] >> 24U);
        digest[i * 4U + 1U] = (uint8_t)(state[i] >> 16U);
        digest[i * 4U + 2U] = (uint8_t)(state[i] >> 8U);
        digest[i * 4U + 3U] = (uint8_t)state[i];
    }
}

bool xx_sha1_init(xx_sha1_context *context)
{
    if (!context) return false;
    xx_rt_memset(context, 0, sizeof(*context));
    context->state[0] = 0x67452301U;
    context->state[1] = 0xefcdab89U;
    context->state[2] = 0x98badcfeU;
    context->state[3] = 0x10325476U;
    context->state[4] = 0xc3d2e1f0U;
    context->initialized = true;
    return true;
}

bool xx_sha256_init(xx_sha256_context *context)
{
    static const uint32_t initial[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    if (!context) return false;
    xx_rt_memset(context, 0, sizeof(*context));
    xx_rt_memcpy(context->state, initial, sizeof(initial));
    context->initialized = true;
    return true;
}

void xx_sha1_update(xx_sha1_context *context, const void *data, size_t size)
{
    if (!context || !context->initialized || (!data && size != 0U)) return;
    sha_update(context->state, &context->total_size, context->buffer, &context->buffer_size, (const uint8_t *)data, size, xx_sha1_blocks);
}

void xx_sha256_update(xx_sha256_context *context, const void *data, size_t size)
{
    if (!context || !context->initialized || (!data && size != 0U)) return;
    sha_update(context->state, &context->total_size, context->buffer, &context->buffer_size, (const uint8_t *)data, size, xx_sha256_blocks);
}

bool xx_sha1_final(xx_sha1_context *context, void *digest, size_t digest_size)
{
    if (!context || !context->initialized || !digest || digest_size < XX_SHA1_DIGEST_SIZE) return false;
    sha_final(context->state, 5U, context->total_size, context->buffer, context->buffer_size, (uint8_t *)digest, xx_sha1_blocks);
    sha_clear(context, sizeof(*context));
    return true;
}

bool xx_sha256_final(xx_sha256_context *context, void *digest, size_t digest_size)
{
    if (!context || !context->initialized || !digest || digest_size < XX_SHA256_DIGEST_SIZE) return false;
    sha_final(context->state, 8U, context->total_size, context->buffer, context->buffer_size, (uint8_t *)digest, xx_sha256_blocks);
    sha_clear(context, sizeof(*context));
    return true;
}

bool xx_sha1_memory(const void *data, size_t size, void *digest)
{
    xx_sha1_context context;
    if ((!data && size != 0U) || !digest) return false;
    xx_sha1_init(&context);
    xx_sha1_update(&context, data, size);
    return xx_sha1_final(&context, digest, XX_SHA1_DIGEST_SIZE);
}

bool xx_sha256_memory(const void *data, size_t size, void *digest)
{
    xx_sha256_context context;
    if ((!data && size != 0U) || !digest) return false;
    xx_sha256_init(&context);
    xx_sha256_update(&context, data, size);
    return xx_sha256_final(&context, digest, XX_SHA256_DIGEST_SIZE);
}
