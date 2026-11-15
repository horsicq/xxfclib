/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/sha512/xx_sha512.h"
#include <string.h>

static void sha_clear(void *data, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size-- != 0U) *p++ = 0U;
}

static uint32_t sha_rotr32(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32U - bits));
}

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

bool xx_sha224_init(xx_sha224_context *context)
{
    static const uint32_t initial[8] = {0xc1059ed8U, 0x367cd507U, 0x3070dd17U, 0xf70e5939U, 0xffc00b31U, 0x68581511U, 0x64f98fa7U, 0xbefa4fa4U};
    if (!context) return false;
    memset(context, 0, sizeof(*context));
    memcpy(context->state, initial, sizeof(initial));
    context->initialized = true;
    return true;
}

bool xx_sha224_update(xx_sha224_context *context, const void *data, size_t size)
{
    const uint8_t *input = (const uint8_t *)data;
    uint32_t schedule[64];
    if (!context || !context->initialized || context->buffer_size >= 64U || (!data && size) ||
        context->total_size > (UINT64_MAX >> 3U) || (uint64_t)size > (UINT64_MAX >> 3U) - context->total_size) return false;
    context->total_size += (uint64_t)size;
    while (size) {
        size_t take = 64U - context->buffer_size;
        if (take > size) take = size;
        memcpy(context->buffer + context->buffer_size, input, take);
        context->buffer_size += take;
        input += take;
        size -= take;
        if (context->buffer_size == 64U) {
            sha256_transform(context->state, context->buffer, schedule);
            context->buffer_size = 0U;
        }
    }
    sha_clear(schedule, sizeof(schedule));
    return true;
}

bool xx_sha224_final(xx_sha224_context *context, void *digest, size_t digest_size)
{
    uint32_t schedule[64];
    uint64_t bits;
    uint8_t *out = (uint8_t *)digest;
    unsigned i;
    if (!context || !context->initialized || context->buffer_size >= 64U ||
        context->total_size > (UINT64_MAX >> 3U) || !digest || digest_size < 28U) return false;
    bits = context->total_size << 3U;
    context->buffer[context->buffer_size++] = 0x80U;
    if (context->buffer_size > 56U) {
        memset(context->buffer + context->buffer_size, 0, 64U - context->buffer_size);
        sha256_transform(context->state, context->buffer, schedule);
        context->buffer_size = 0U;
    }
    memset(context->buffer + context->buffer_size, 0, 56U - context->buffer_size);
    for (i = 0U; i < 8U; ++i) context->buffer[63U - i] = (uint8_t)(bits >> (8U * i));
    sha256_transform(context->state, context->buffer, schedule);
    for (i = 0U; i < 28U; ++i) out[i] = (uint8_t)(context->state[i / 4U] >> (24U - (i % 4U) * 8U));
    sha_clear(schedule, sizeof(schedule));
    sha_clear(context, sizeof(*context));
    return true;
}

bool xx_sha224_memory(const void *data, size_t size, void *digest)
{
    xx_sha224_context context;
    bool ok;
    if ((!data && size) || !digest) return false;
    (void)xx_sha224_init(&context);
    ok = xx_sha224_update(&context, data, size) && xx_sha224_final(&context, digest, 28U);
    sha_clear(&context, sizeof(context));
    return ok;
}

static uint64_t sha_rotr64(uint64_t value, unsigned bits)
{
    return (value >> bits) | (value << (64U - bits));
}

/* First 64 bits of fractional cube roots of the first 80 primes (FIPS 180-4). */
static const uint64_t SHA512_K[80] = {
    UINT64_C(0x428a2f98d728ae22), UINT64_C(0x7137449123ef65cd), UINT64_C(0xb5c0fbcfec4d3b2f), UINT64_C(0xe9b5dba58189dbbc),
    UINT64_C(0x3956c25bf348b538), UINT64_C(0x59f111f1b605d019), UINT64_C(0x923f82a4af194f9b), UINT64_C(0xab1c5ed5da6d8118),
    UINT64_C(0xd807aa98a3030242), UINT64_C(0x12835b0145706fbe), UINT64_C(0x243185be4ee4b28c), UINT64_C(0x550c7dc3d5ffb4e2),
    UINT64_C(0x72be5d74f27b896f), UINT64_C(0x80deb1fe3b1696b1), UINT64_C(0x9bdc06a725c71235), UINT64_C(0xc19bf174cf692694),
    UINT64_C(0xe49b69c19ef14ad2), UINT64_C(0xefbe4786384f25e3), UINT64_C(0x0fc19dc68b8cd5b5), UINT64_C(0x240ca1cc77ac9c65),
    UINT64_C(0x2de92c6f592b0275), UINT64_C(0x4a7484aa6ea6e483), UINT64_C(0x5cb0a9dcbd41fbd4), UINT64_C(0x76f988da831153b5),
    UINT64_C(0x983e5152ee66dfab), UINT64_C(0xa831c66d2db43210), UINT64_C(0xb00327c898fb213f), UINT64_C(0xbf597fc7beef0ee4),
    UINT64_C(0xc6e00bf33da88fc2), UINT64_C(0xd5a79147930aa725), UINT64_C(0x06ca6351e003826f), UINT64_C(0x142929670a0e6e70),
    UINT64_C(0x27b70a8546d22ffc), UINT64_C(0x2e1b21385c26c926), UINT64_C(0x4d2c6dfc5ac42aed), UINT64_C(0x53380d139d95b3df),
    UINT64_C(0x650a73548baf63de), UINT64_C(0x766a0abb3c77b2a8), UINT64_C(0x81c2c92e47edaee6), UINT64_C(0x92722c851482353b),
    UINT64_C(0xa2bfe8a14cf10364), UINT64_C(0xa81a664bbc423001), UINT64_C(0xc24b8b70d0f89791), UINT64_C(0xc76c51a30654be30),
    UINT64_C(0xd192e819d6ef5218), UINT64_C(0xd69906245565a910), UINT64_C(0xf40e35855771202a), UINT64_C(0x106aa07032bbd1b8),
    UINT64_C(0x19a4c116b8d2d0c8), UINT64_C(0x1e376c085141ab53), UINT64_C(0x2748774cdf8eeb99), UINT64_C(0x34b0bcb5e19b48a8),
    UINT64_C(0x391c0cb3c5c95a63), UINT64_C(0x4ed8aa4ae3418acb), UINT64_C(0x5b9cca4f7763e373), UINT64_C(0x682e6ff3d6b2b8a3),
    UINT64_C(0x748f82ee5defb2fc), UINT64_C(0x78a5636f43172f60), UINT64_C(0x84c87814a1f0ab72), UINT64_C(0x8cc702081a6439ec),
    UINT64_C(0x90befffa23631e28), UINT64_C(0xa4506cebde82bde9), UINT64_C(0xbef9a3f7b2c67915), UINT64_C(0xc67178f2e372532b),
    UINT64_C(0xca273eceea26619c), UINT64_C(0xd186b8c721c0c207), UINT64_C(0xeada7dd6cde0eb1e), UINT64_C(0xf57d4f7fee6ed178),
    UINT64_C(0x06f067aa72176fba), UINT64_C(0x0a637dc5a2c898a6), UINT64_C(0x113f9804bef90dae), UINT64_C(0x1b710b35131c471b),
    UINT64_C(0x28db77f523047d84), UINT64_C(0x32caab7b40c72493), UINT64_C(0x3c9ebe0a15c9bebc), UINT64_C(0x431d67c49c100d4c),
    UINT64_C(0x4cc5d4becb3e42b6), UINT64_C(0x597f299cfc657e2a), UINT64_C(0x5fcb6fab3ad6faec), UINT64_C(0x6c44198c4a475817)
};

static void sha512_transform(uint64_t state[8], const uint8_t block[128])
{
    uint64_t w[80], a, b, c, d, e, f, g, h;
    unsigned i, j;
    for (i = 0U; i < 16U; ++i) {
        w[i] = 0U;
        for (j = 0U; j < 8U; ++j) w[i] = (w[i] << 8U) | block[8U * i + j];
    }
    for (i = 16U; i < 80U; ++i) {
        uint64_t s0 = sha_rotr64(w[i-15U], 1U) ^ sha_rotr64(w[i-15U], 8U) ^ (w[i-15U] >> 7U);
        uint64_t s1 = sha_rotr64(w[i-2U], 19U) ^ sha_rotr64(w[i-2U], 61U) ^ (w[i-2U] >> 6U);
        w[i] = w[i-16U] + s0 + w[i-7U] + s1;
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    for (i = 0U; i < 80U; ++i) {
        uint64_t s1 = sha_rotr64(e, 14U) ^ sha_rotr64(e, 18U) ^ sha_rotr64(e, 41U);
        uint64_t t1 = h + s1 + ((e & f) ^ (~e & g)) + SHA512_K[i] + w[i];
        uint64_t s0 = sha_rotr64(a, 28U) ^ sha_rotr64(a, 34U) ^ sha_rotr64(a, 39U);
        uint64_t t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    sha_clear(w, sizeof(w));
}

static bool sha512_init(xx_sha512_context *context, const uint64_t initial[8], unsigned digest_size)
{
    if (!context) return false;
    memset(context, 0, sizeof(*context));
    memcpy(context->state, initial, 8U * sizeof(uint64_t));
    context->digest_size = digest_size;
    context->initialized = true;
    return true;
}

static bool sha512_update(xx_sha512_context *context, const void *data, size_t size, unsigned digest_size)
{
    const uint8_t *input = (const uint8_t *)data;
    uint64_t low, high;
    if (!context || !context->initialized || context->buffer_size >= 128U ||
        context->digest_size != digest_size || (!data && size)) return false;
    low = context->total_low + (uint64_t)size;
    high = context->total_high + (low < context->total_low ? 1U : 0U);
    if (high < context->total_high || high > (UINT64_MAX >> 3U)) return false;
    context->total_low = low;
    context->total_high = high;
    while (size) {
        size_t take = 128U - context->buffer_size;
        if (take > size) take = size;
        memcpy(context->buffer + context->buffer_size, input, take);
        context->buffer_size += take;
        input += take;
        size -= take;
        if (context->buffer_size == 128U) {
            sha512_transform(context->state, context->buffer);
            context->buffer_size = 0U;
        }
    }
    return true;
}

static bool sha512_final(xx_sha512_context *context, void *digest, size_t digest_size, unsigned required)
{
    uint8_t *out = (uint8_t *)digest;
    uint64_t high, low;
    unsigned i;
    if (!context || !context->initialized || context->buffer_size >= 128U || context->digest_size != required ||
        context->total_high > (UINT64_MAX >> 3U) || !digest || digest_size < required) return false;
    low = context->total_low << 3U;
    high = (context->total_high << 3U) | (context->total_low >> 61U);
    context->buffer[context->buffer_size++] = 0x80U;
    if (context->buffer_size > 112U) {
        memset(context->buffer + context->buffer_size, 0, 128U - context->buffer_size);
        sha512_transform(context->state, context->buffer);
        context->buffer_size = 0U;
    }
    memset(context->buffer + context->buffer_size, 0, 112U - context->buffer_size);
    for (i = 0U; i < 8U; ++i) {
        context->buffer[119U-i] = (uint8_t)(high >> (8U * i));
        context->buffer[127U-i] = (uint8_t)(low >> (8U * i));
    }
    sha512_transform(context->state, context->buffer);
    for (i = 0U; i < required; ++i) out[i] = (uint8_t)(context->state[i/8U] >> (56U - (i%8U)*8U));
    sha_clear(context, sizeof(*context));
    return true;
}

bool xx_sha384_init(xx_sha384_context *context)
{
    static const uint64_t initial[8] = {UINT64_C(0xcbbb9d5dc1059ed8), UINT64_C(0x629a292a367cd507), UINT64_C(0x9159015a3070dd17), UINT64_C(0x152fecd8f70e5939), UINT64_C(0x67332667ffc00b31), UINT64_C(0x8eb44a8768581511), UINT64_C(0xdb0c2e0d64f98fa7), UINT64_C(0x47b5481dbefa4fa4)};
    return sha512_init(context, initial, 48U);
}

bool xx_sha384_update(xx_sha384_context *context, const void *data, size_t size)
{
    return sha512_update(context, data, size, 48U);
}

bool xx_sha384_final(xx_sha384_context *context, void *digest, size_t digest_size)
{
    return sha512_final(context, digest, digest_size, 48U);
}

bool xx_sha384_memory(const void *data, size_t size, void *digest)
{
    xx_sha384_context context;
    bool ok;
    if ((!data && size) || !digest) return false;
    (void)xx_sha384_init(&context);
    ok = xx_sha384_update(&context, data, size) && xx_sha384_final(&context, digest, 48U);
    sha_clear(&context, sizeof(context));
    return ok;
}

bool xx_sha512_init(xx_sha512_context *context)
{
    static const uint64_t initial[8] = {UINT64_C(0x6a09e667f3bcc908), UINT64_C(0xbb67ae8584caa73b), UINT64_C(0x3c6ef372fe94f82b), UINT64_C(0xa54ff53a5f1d36f1), UINT64_C(0x510e527fade682d1), UINT64_C(0x9b05688c2b3e6c1f), UINT64_C(0x1f83d9abfb41bd6b), UINT64_C(0x5be0cd19137e2179)};
    return sha512_init(context, initial, 64U);
}

bool xx_sha512_update(xx_sha512_context *context, const void *data, size_t size)
{
    return sha512_update(context, data, size, 64U);
}

bool xx_sha512_final(xx_sha512_context *context, void *digest, size_t digest_size)
{
    return sha512_final(context, digest, digest_size, 64U);
}

bool xx_sha512_memory(const void *data, size_t size, void *digest)
{
    xx_sha512_context context;
    bool ok;
    if ((!data && size) || !digest) return false;
    (void)xx_sha512_init(&context);
    ok = xx_sha512_update(&context, data, size) && xx_sha512_final(&context, digest, 64U);
    sha_clear(&context, sizeof(context));
    return ok;
}

bool xx_sha512_224_init(xx_sha512_224_context *context)
{
    static const uint64_t initial[8] = {UINT64_C(0x8c3d37c819544da2), UINT64_C(0x73e1996689dcd4d6), UINT64_C(0x1dfab7ae32ff9c82), UINT64_C(0x679dd514582f9fcf), UINT64_C(0x0f6d2b697bd44da8), UINT64_C(0x77e36f7304c48942), UINT64_C(0x3f9d85a86a1d36c8), UINT64_C(0x1112e6ad91d692a1)};
    return sha512_init(context, initial, 28U);
}

bool xx_sha512_224_update(xx_sha512_224_context *context, const void *data, size_t size)
{
    return sha512_update(context, data, size, 28U);
}

bool xx_sha512_224_final(xx_sha512_224_context *context, void *digest, size_t digest_size)
{
    return sha512_final(context, digest, digest_size, 28U);
}

bool xx_sha512_224_memory(const void *data, size_t size, void *digest)
{
    xx_sha512_224_context context;
    bool ok;
    if ((!data && size) || !digest) return false;
    (void)xx_sha512_224_init(&context);
    ok = xx_sha512_224_update(&context, data, size) && xx_sha512_224_final(&context, digest, 28U);
    sha_clear(&context, sizeof(context));
    return ok;
}

bool xx_sha512_256_init(xx_sha512_256_context *context)
{
    static const uint64_t initial[8] = {UINT64_C(0x22312194fc2bf72c), UINT64_C(0x9f555fa3c84c64c2), UINT64_C(0x2393b86b6f53b151), UINT64_C(0x963877195940eabd), UINT64_C(0x96283ee2a88effe3), UINT64_C(0xbe5e1e2553863992), UINT64_C(0x2b0199fc2c85b8aa), UINT64_C(0x0eb72ddc81c52ca2)};
    return sha512_init(context, initial, 32U);
}

bool xx_sha512_256_update(xx_sha512_256_context *context, const void *data, size_t size)
{
    return sha512_update(context, data, size, 32U);
}

bool xx_sha512_256_final(xx_sha512_256_context *context, void *digest, size_t digest_size)
{
    return sha512_final(context, digest, digest_size, 32U);
}

bool xx_sha512_256_memory(const void *data, size_t size, void *digest)
{
    xx_sha512_256_context context;
    bool ok;
    if ((!data && size) || !digest) return false;
    (void)xx_sha512_256_init(&context);
    ok = xx_sha512_256_update(&context, data, size) && xx_sha512_256_final(&context, digest, 32U);
    sha_clear(&context, sizeof(context));
    return ok;
}

