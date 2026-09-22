/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_hash.c
 * @brief MD5, SHA-1 and SHA-256, sharing one context and one buffering path.
 *
 * The three algorithms differ only in their compression function and in how
 * the final state is serialised; the block accumulation, the length counter
 * and the padding rule are identical. So the buffering is written once and
 * the per-algorithm work lives in three transforms.
 */

#include "xxfclib/algo/hash/xx_hash.h"

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"

#define XX_HASH_BLOCK 64U
/* Chunk used when digesting a device. Large enough that the per-read
 * overhead disappears, small enough to stay off the stack. */
#define XX_HASH_IO_CHUNK (64U * 1024U)

static uint32_t hash_rotl32(uint32_t value, unsigned bits) {
    return (uint32_t)((value << bits) | (value >> (32U - bits)));
}

static uint32_t hash_rotr32(uint32_t value, unsigned bits) {
    return (uint32_t)((value >> bits) | (value << (32U - bits)));
}

/* ------------------------------------------------------------------- MD5 -- */

static const uint32_t MD5_K[64] = {
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU,
    0x4787c62aU, 0xa8304613U, 0xfd469501U, 0x698098d8U, 0x8b44f7afU,
    0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U, 0xa679438eU,
    0x49b40821U, 0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
    0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U, 0x21e1cde6U,
    0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U,
    0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U,
    0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
    0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U, 0xd9d4d039U,
    0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U, 0xf4292244U, 0x432aff97U,
    0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU,
    0x85845dd1U, 0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
    0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U};

static const unsigned char MD5_S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static void hash_md5_transform(uint32_t *state, const uint8_t *block) {
    uint32_t m[16];
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    unsigned i;

    for (i = 0; i < 16U; ++i) {
        m[i] = (uint32_t)block[i * 4U] | ((uint32_t)block[i * 4U + 1U] << 8) |
               ((uint32_t)block[i * 4U + 2U] << 16) |
               ((uint32_t)block[i * 4U + 3U] << 24);
    }

    for (i = 0; i < 64U; ++i) {
        uint32_t f;
        unsigned g;
        if (i < 16U) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32U) {
            f = (d & b) | (~d & c);
            g = (5U * i + 1U) & 15U;
        } else if (i < 48U) {
            f = b ^ c ^ d;
            g = (3U * i + 5U) & 15U;
        } else {
            f = c ^ (b | ~d);
            g = (7U * i) & 15U;
        }
        f += a + MD5_K[i] + m[g];
        a = d;
        d = c;
        c = b;
        b += hash_rotl32(f, MD5_S[i]);
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

/* ----------------------------------------------------------------- SHA-1 -- */

static void hash_sha1_transform(uint32_t *state, const uint8_t *block) {
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    unsigned i;

    for (i = 0; i < 16U; ++i) {
        w[i] = ((uint32_t)block[i * 4U] << 24) |
               ((uint32_t)block[i * 4U + 1U] << 16) |
               ((uint32_t)block[i * 4U + 2U] << 8) |
               (uint32_t)block[i * 4U + 3U];
    }
    for (i = 16U; i < 80U; ++i) {
        w[i] = hash_rotl32(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];

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
            uint32_t temp = hash_rotl32(a, 5U) + f + e + k + w[i];
            e = d;
            d = c;
            c = hash_rotl32(b, 30U);
            b = a;
            a = temp;
        }
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

/* --------------------------------------------------------------- SHA-256 -- */

static const uint32_t SHA256_K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

static void hash_sha256_transform(uint32_t *state, const uint8_t *block) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned i;

    for (i = 0; i < 16U; ++i) {
        w[i] = ((uint32_t)block[i * 4U] << 24) |
               ((uint32_t)block[i * 4U + 1U] << 16) |
               ((uint32_t)block[i * 4U + 2U] << 8) |
               (uint32_t)block[i * 4U + 3U];
    }
    for (i = 16U; i < 64U; ++i) {
        uint32_t s0 = hash_rotr32(w[i - 15U], 7U) ^
                      hash_rotr32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3);
        uint32_t s1 = hash_rotr32(w[i - 2U], 17U) ^
                      hash_rotr32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64U; ++i) {
        uint32_t s1 = hash_rotr32(e, 6U) ^ hash_rotr32(e, 11U) ^
                      hash_rotr32(e, 25U);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + SHA256_K[i] + w[i];
        uint32_t s0 = hash_rotr32(a, 2U) ^ hash_rotr32(a, 13U) ^
                      hash_rotr32(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e;
        e = d + t1;
        d = c; c = b; b = a;
        a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/* ------------------------------------------------------- shared plumbing -- */

static void hash_transform(xx_hash_context *ctx, const uint8_t *block) {
    switch (ctx->type) {
        case XX_HASH_MD5: hash_md5_transform(ctx->state, block); break;
        case XX_HASH_SHA1: hash_sha1_transform(ctx->state, block); break;
        default: hash_sha256_transform(ctx->state, block); break;
    }
}

size_t xx_hash_digest_size(xx_hash_type_t type) {
    switch (type) {
        case XX_HASH_MD5: return XX_MD5_DIGEST_SIZE;
        case XX_HASH_SHA1: return XX_SHA1_DIGEST_SIZE;
        case XX_HASH_SHA256: return XX_SHA256_DIGEST_SIZE;
        default: return 0U;
    }
}

bool xx_hash_init(xx_hash_context *ctx, xx_hash_type_t type) {
    if (!ctx) return false;
    if (xx_hash_digest_size(type) == 0U) return false;

    xx_rt_memset(ctx, 0, sizeof(*ctx));
    ctx->type = type;

    switch (type) {
        case XX_HASH_MD5:
            ctx->state[0] = 0x67452301U;
            ctx->state[1] = 0xefcdab89U;
            ctx->state[2] = 0x98badcfeU;
            ctx->state[3] = 0x10325476U;
            break;
        case XX_HASH_SHA1:
            ctx->state[0] = 0x67452301U;
            ctx->state[1] = 0xefcdab89U;
            ctx->state[2] = 0x98badcfeU;
            ctx->state[3] = 0x10325476U;
            ctx->state[4] = 0xc3d2e1f0U;
            break;
        default:
            ctx->state[0] = 0x6a09e667U;
            ctx->state[1] = 0xbb67ae85U;
            ctx->state[2] = 0x3c6ef372U;
            ctx->state[3] = 0xa54ff53aU;
            ctx->state[4] = 0x510e527fU;
            ctx->state[5] = 0x9b05688cU;
            ctx->state[6] = 0x1f83d9abU;
            ctx->state[7] = 0x5be0cd19U;
            break;
    }
    ctx->initialized = true;
    return true;
}

void xx_hash_update(xx_hash_context *ctx, const void *data, size_t size) {
    const uint8_t *input = (const uint8_t *)data;
    size_t taken = 0U;

    if (!ctx || !ctx->initialized || (!data && size != 0U)) return;

    ctx->length += (uint64_t)size;

    /* Top up a partial block first, so the fast path below always sees a
     * block-aligned start. */
    if (ctx->buffered != 0U) {
        size_t need = XX_HASH_BLOCK - ctx->buffered;
        size_t copy = (size < need) ? size : need;
        xx_rt_memcpy(ctx->buffer + ctx->buffered, input, copy);
        ctx->buffered += copy;
        taken = copy;
        if (ctx->buffered < XX_HASH_BLOCK) return;
        hash_transform(ctx, ctx->buffer);
        ctx->buffered = 0U;
    }

    while (size - taken >= XX_HASH_BLOCK) {
        hash_transform(ctx, input + taken);
        taken += XX_HASH_BLOCK;
    }

    if (taken < size) {
        ctx->buffered = size - taken;
        xx_rt_memcpy(ctx->buffer, input + taken, ctx->buffered);
    }
}

bool xx_hash_final(xx_hash_context *ctx, void *out, size_t out_size) {
    uint8_t *digest = (uint8_t *)out;
    uint64_t bits;
    size_t size, i;
    unsigned words;

    if (!ctx || !ctx->initialized || !out) return false;
    size = xx_hash_digest_size(ctx->type);
    if (out_size < size) return false;

    bits = ctx->length * 8U;

    /* Padding: a 0x80 byte, zeros, then the bit count. Same rule for all
     * three; only the byte order of the count differs. */
    ctx->buffer[ctx->buffered++] = 0x80U;
    if (ctx->buffered > XX_HASH_BLOCK - 8U) {
        xx_rt_memset(ctx->buffer + ctx->buffered, 0,
                     XX_HASH_BLOCK - ctx->buffered);
        hash_transform(ctx, ctx->buffer);
        ctx->buffered = 0U;
    }
    xx_rt_memset(ctx->buffer + ctx->buffered, 0,
                 (XX_HASH_BLOCK - 8U) - ctx->buffered);

    if (ctx->type == XX_HASH_MD5) {
        for (i = 0; i < 8U; ++i) {
            ctx->buffer[XX_HASH_BLOCK - 8U + i] =
                (uint8_t)((bits >> (8U * i)) & 0xFFU);
        }
    } else {
        for (i = 0; i < 8U; ++i) {
            ctx->buffer[XX_HASH_BLOCK - 1U - i] =
                (uint8_t)((bits >> (8U * i)) & 0xFFU);
        }
    }
    hash_transform(ctx, ctx->buffer);

    words = (unsigned)(size / 4U);
    for (i = 0; i < words; ++i) {
        uint32_t value = ctx->state[i];
        if (ctx->type == XX_HASH_MD5) {
            digest[i * 4U] = (uint8_t)(value & 0xFFU);
            digest[i * 4U + 1U] = (uint8_t)((value >> 8) & 0xFFU);
            digest[i * 4U + 2U] = (uint8_t)((value >> 16) & 0xFFU);
            digest[i * 4U + 3U] = (uint8_t)((value >> 24) & 0xFFU);
        } else {
            digest[i * 4U] = (uint8_t)((value >> 24) & 0xFFU);
            digest[i * 4U + 1U] = (uint8_t)((value >> 16) & 0xFFU);
            digest[i * 4U + 2U] = (uint8_t)((value >> 8) & 0xFFU);
            digest[i * 4U + 3U] = (uint8_t)(value & 0xFFU);
        }
    }

    /* Leave nothing recoverable behind, and make reuse without a fresh init
     * fail loudly rather than continue a finished digest. */
    xx_rt_memset(ctx, 0, sizeof(*ctx));
    return true;
}

/* ----------------------------------------------------------- one-shot API -- */

bool xx_hash_memory(xx_hash_type_t type, const void *data, size_t size,
                    void *out, size_t out_size) {
    xx_hash_context ctx;
    if (!data && size != 0U) return false;
    if (!xx_hash_init(&ctx, type)) return false;
    xx_hash_update(&ctx, data, size);
    return xx_hash_final(&ctx, out, out_size);
}

bool xx_hash_device(xx_hash_type_t type, xx_io_device *dev, int64_t offset,
                    int64_t size, void *out, size_t out_size,
                    xx_pd_struct *pd) {
    xx_hash_context ctx;
    uint8_t *chunk;
    int64_t total;
    int64_t done = 0;
    bool ok = true;

    if (!dev || offset < 0) return false;
    total = xx_io_total_size(dev);
    if (total < 0 || offset > total) return false;
    if (size < 0) size = total - offset;
    if (size > total - offset) return false;
    if (!xx_hash_init(&ctx, type)) return false;
    if (xx_io_seek64(dev, offset, SEEK_SET) != 0) return false;

    chunk = (uint8_t *)xx_mem_alloc(XX_HASH_IO_CHUNK);
    if (!chunk) return false;

    while (done < size) {
        int64_t want = size - done;
        ssize_t got;
        if (want > (int64_t)XX_HASH_IO_CHUNK) want = (int64_t)XX_HASH_IO_CHUNK;
        got = xx_io_read(dev, chunk, (size_t)want);
        if (got <= 0 || got > want) { ok = false; break; }
        xx_hash_update(&ctx, chunk, (size_t)got);
        done += got;
        if (pd && xx_pd_is_stopped(pd)) { ok = false; break; }
    }

    xx_mem_free(chunk);
    if (!ok) {
        xx_rt_memset(&ctx, 0, sizeof(ctx));
        return false;
    }
    return xx_hash_final(&ctx, out, out_size);
}

bool xx_md5_memory(const void *data, size_t size, void *out) {
    return xx_hash_memory(XX_HASH_MD5, data, size, out, XX_MD5_DIGEST_SIZE);
}

bool xx_sha1_memory(const void *data, size_t size, void *out) {
    return xx_hash_memory(XX_HASH_SHA1, data, size, out, XX_SHA1_DIGEST_SIZE);
}

bool xx_sha256_memory(const void *data, size_t size, void *out) {
    return xx_hash_memory(XX_HASH_SHA256, data, size, out,
                          XX_SHA256_DIGEST_SIZE);
}

bool xx_hash_to_hex(const void *digest, size_t digest_size, char *out,
                    size_t out_size) {
    static const char HEX[] = "0123456789abcdef";
    const uint8_t *bytes = (const uint8_t *)digest;
    size_t i;

    if (!digest || !out) return false;
    if (out_size < digest_size * 2U + 1U) return false;
    for (i = 0; i < digest_size; ++i) {
        out[i * 2U] = HEX[(bytes[i] >> 4) & 0x0FU];
        out[i * 2U + 1U] = HEX[bytes[i] & 0x0FU];
    }
    out[digest_size * 2U] = '\0';
    return true;
}

bool xx_hash_equal(const void *a, const void *b, size_t size) {
    if (!a || !b) return false;
    return xx_rt_memcmp(a, b, size) == 0;
}
