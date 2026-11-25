/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/nchash/xx_nchash.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include <string.h>

/* Helper bit rotation functions */
static inline uint32_t xx_nchash_rotl32(uint32_t x, unsigned r)
{
    return (x << r) | (x >> (32U - r));
}

static inline uint32_t xx_nchash_rotr32(uint32_t x, unsigned r)
{
    return (x >> r) | (x << (32U - r));
}

static inline uint64_t xx_nchash_rotl64(uint64_t x, unsigned r)
{
    return (x << r) | (x >> (64U - r));
}

static inline uint64_t xx_nchash_rotr64(uint64_t x, unsigned r)
{
    return (x >> r) | (x << (64U - r));
}

static inline uint32_t xx_nchash_load32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t xx_nchash_load64_le(const uint8_t *p)
{
    return (uint64_t)xx_nchash_load32_le(p) | ((uint64_t)xx_nchash_load32_le(p + 4) << 32);
}

static inline void xx_nchash_store32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline void xx_nchash_store64_be(uint8_t *p, uint64_t v)
{
    xx_nchash_store32_be(p, (uint32_t)(v >> 32));
    xx_nchash_store32_be(p + 4, (uint32_t)v);
}

static inline void xx_nchash_store32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void xx_nchash_store64_le(uint8_t *p, uint64_t v)
{
    xx_nchash_store32_le(p, (uint32_t)v);
    xx_nchash_store32_le(p + 4, (uint32_t)(v >> 32));
}

/* ========================================================================= */
/* --- FNV Hashes                                                        --- */
/* ========================================================================= */

#define FNV1_32_INIT 0x811C9DC5U
#define FNV1_32_PRIME 0x01000193U
#define FNV1_64_INIT 0xCBF29CE484222325ULL
#define FNV1_64_PRIME 0x100000001B3ULL

uint32_t xx_nchash_fnv1_32(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = FNV1_32_INIT;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = (h * FNV1_32_PRIME) ^ p[i];
        }
    }
    return h;
}

uint32_t xx_nchash_fnv1a_32(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = FNV1_32_INIT;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = (h ^ p[i]) * FNV1_32_PRIME;
        }
    }
    return h;
}

uint64_t xx_nchash_fnv1_64(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = FNV1_64_INIT;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = (h * FNV1_64_PRIME) ^ p[i];
        }
    }
    return h;
}

uint64_t xx_nchash_fnv1a_64(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = FNV1_64_INIT;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = (h ^ p[i]) * FNV1_64_PRIME;
        }
    }
    return h;
}

/* ========================================================================= */
/* --- Classic Non-Crypto 32-bit Hashes                                  --- */
/* ========================================================================= */

uint32_t xx_nchash_djb2(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 5381U;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = ((h << 5) + h) + p[i];
        }
    }
    return h;
}

uint32_t xx_nchash_sdbm(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0U;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = p[i] + (h << 6) + (h << 16) - h;
        }
    }
    return h;
}

uint32_t xx_nchash_elf(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0U;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = (h << 4) + p[i];
            uint32_t g = h & 0xF0000000U;
            if (g) h ^= (g >> 24);
            h &= ~g;
        }
    }
    return h;
}

uint32_t xx_nchash_pjw(const void *data, size_t size)
{
    return xx_nchash_elf(data, size);
}

uint32_t xx_nchash_bkdr(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0U;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = h * 131U + p[i];
        }
    }
    return h;
}

uint32_t xx_nchash_ap(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0xAAAAAAAAU;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            if ((i & 1) == 0) {
                h ^= ((h << 7) ^ p[i] ^ (h >> 3));
            } else {
                h ^= (~((h << 11) ^ p[i] ^ (h >> 5)));
            }
        }
    }
    return h;
}

uint32_t xx_nchash_dek(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = (uint32_t)size;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = xx_nchash_rotl32(h, 5) ^ p[i];
        }
    }
    return h;
}

uint32_t xx_nchash_js(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 1315423911U;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h ^= ((h << 5) + p[i] + (h >> 2));
        }
    }
    return h;
}

uint32_t xx_nchash_rs(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t a = 63689U;
    uint32_t h = 0U;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h = h * a + p[i];
            a *= 378551U;
        }
    }
    return h;
}

uint32_t xx_nchash_oat(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0U;
    if (p) {
        for (size_t i = 0; i < size; ++i) {
            h += p[i];
            h += (h << 10);
            h ^= (h >> 6);
        }
    }
    h += (h << 3);
    h ^= (h >> 11);
    h += (h << 15);
    return h;
}

/* ========================================================================= */
/* --- MurmurHash3                                                       --- */
/* ========================================================================= */

static inline uint32_t xx_nchash_fmix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6BU;
    h ^= h >> 13;
    h *= 0xC2B2AE35U;
    h ^= h >> 16;
    return h;
}

static inline uint64_t xx_nchash_fmix64(uint64_t k)
{
    k ^= k >> 33;
    k *= 0xFF51AFD7ED558CCDULL;
    k ^= k >> 33;
    k *= 0xC4CEB9FE1A85EC53ULL;
    k ^= k >> 33;
    return k;
}

uint32_t xx_nchash_murmur3_32(const void *data, size_t size, uint32_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = seed;
    const uint32_t c1 = 0xCC9E2D51U;
    const uint32_t c2 = 0x1B873593U;
    size_t nblocks = size / 4;

    if (p) {
        for (size_t i = 0; i < nblocks; ++i) {
            uint32_t k = xx_nchash_load32_le(p + i * 4);
            k *= c1;
            k = xx_nchash_rotl32(k, 15);
            k *= c2;
            h ^= k;
            h = xx_nchash_rotl32(h, 13);
            h = h * 5U + 0xE6546B64U;
        }

        const uint8_t *tail = p + nblocks * 4;
        uint32_t k1 = 0;
        switch (size & 3) {
            case 3: k1 ^= (uint32_t)tail[2] << 16; /* fallthrough */
            case 2: k1 ^= (uint32_t)tail[1] << 8;  /* fallthrough */
            case 1:
                k1 ^= (uint32_t)tail[0];
                k1 *= c1;
                k1 = xx_nchash_rotl32(k1, 15);
                k1 *= c2;
                h ^= k1;
                break;
            default: break;
        }
    }

    h ^= (uint32_t)size;
    return xx_nchash_fmix32(h);
}

void xx_nchash_murmur3_128(const void *data, size_t size, uint64_t seed, uint8_t out[16])
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h1 = seed;
    uint64_t h2 = seed;
    const uint64_t c1 = 0x87C37B91114253D5ULL;
    const uint64_t c2 = 0x4CF5AD4327459204ULL;
    size_t nblocks = size / 16;

    if (p) {
        for (size_t i = 0; i < nblocks; ++i) {
            uint64_t k1 = xx_nchash_load64_le(p + i * 16);
            uint64_t k2 = xx_nchash_load64_le(p + i * 16 + 8);

            k1 *= c1;
            k1 = xx_nchash_rotl64(k1, 31);
            k1 *= c2;
            h1 ^= k1;
            h1 = xx_nchash_rotl64(h1, 27);
            h1 += h2;
            h1 = h1 * 5ULL + 0x52DCE729ULL;

            k2 *= c2;
            k2 = xx_nchash_rotl64(k2, 33);
            k2 *= c1;
            h2 ^= k2;
            h2 = xx_nchash_rotl64(h2, 31);
            h2 += h1;
            h2 = h2 * 5ULL + 0x38495AB5ULL;
        }

        const uint8_t *tail = p + nblocks * 16;
        uint64_t k1 = 0, k2 = 0;
        size_t rem = size & 15;
        switch (rem) {
            case 15: k2 ^= (uint64_t)tail[14] << 48; /* fallthrough */
            case 14: k2 ^= (uint64_t)tail[13] << 40; /* fallthrough */
            case 13: k2 ^= (uint64_t)tail[12] << 32; /* fallthrough */
            case 12: k2 ^= (uint64_t)tail[11] << 24; /* fallthrough */
            case 11: k2 ^= (uint64_t)tail[10] << 16; /* fallthrough */
            case 10: k2 ^= (uint64_t)tail[9] << 8;   /* fallthrough */
            case 9:
                k2 ^= (uint64_t)tail[8];
                k2 *= c2;
                k2 = xx_nchash_rotl64(k2, 33);
                k2 *= c1;
                h2 ^= k2;
                /* fallthrough */
            case 8: k1 ^= (uint64_t)tail[7] << 56; /* fallthrough */
            case 7: k1 ^= (uint64_t)tail[6] << 48; /* fallthrough */
            case 6: k1 ^= (uint64_t)tail[5] << 40; /* fallthrough */
            case 5: k1 ^= (uint64_t)tail[4] << 32; /* fallthrough */
            case 4: k1 ^= (uint64_t)tail[3] << 24; /* fallthrough */
            case 3: k1 ^= (uint64_t)tail[2] << 16; /* fallthrough */
            case 2: k1 ^= (uint64_t)tail[1] << 8;  /* fallthrough */
            case 1:
                k1 ^= (uint64_t)tail[0];
                k1 *= c1;
                k1 = xx_nchash_rotl64(k1, 31);
                k1 *= c2;
                h1 ^= k1;
                break;
            default: break;
        }
    }

    h1 ^= (uint64_t)size;
    h2 ^= (uint64_t)size;
    h1 += h2;
    h2 += h1;
    h1 = xx_nchash_fmix64(h1);
    h2 = xx_nchash_fmix64(h2);
    h1 += h2;
    h2 += h1;

    if (out) {
        xx_nchash_store64_le(out, h1);
        xx_nchash_store64_le(out + 8, h2);
    }
}

/* ========================================================================= */
/* --- xxHash32 / xxHash64                                               --- */
/* ========================================================================= */

#define XXH32_PRIME1 0x9E3779B1U
#define XXH32_PRIME2 0x85EBCA77U
#define XXH32_PRIME3 0xC2B2AE3DU
#define XXH32_PRIME4 0x27D4EB2FU
#define XXH32_PRIME5 0x165667B1U

uint32_t xx_nchash_xxhash32(const void *data, size_t size, uint32_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h;

    if (size >= 16 && p) {
        uint32_t v1 = seed + XXH32_PRIME1 + XXH32_PRIME2;
        uint32_t v2 = seed + XXH32_PRIME2;
        uint32_t v3 = seed;
        uint32_t v4 = seed - XXH32_PRIME1;

        size_t nblocks = size / 16;
        for (size_t i = 0; i < nblocks; ++i) {
            v1 = xx_nchash_rotl32(v1 + xx_nchash_load32_le(p + i * 16) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            v2 = xx_nchash_rotl32(v2 + xx_nchash_load32_le(p + i * 16 + 4) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            v3 = xx_nchash_rotl32(v3 + xx_nchash_load32_le(p + i * 16 + 8) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            v4 = xx_nchash_rotl32(v4 + xx_nchash_load32_le(p + i * 16 + 12) * XXH32_PRIME2, 13) * XXH32_PRIME1;
        }
        h = xx_nchash_rotl32(v1, 1) + xx_nchash_rotl32(v2, 7) + xx_nchash_rotl32(v3, 12) + xx_nchash_rotl32(v4, 18);
    } else {
        h = seed + XXH32_PRIME5;
    }

    h += (uint32_t)size;
    size_t idx = (size >= 16) ? (size / 16) * 16 : 0;
    if (p) {
        while (idx + 4 <= size) {
            h += xx_nchash_load32_le(p + idx) * XXH32_PRIME3;
            h = xx_nchash_rotl32(h, 17) * XXH32_PRIME4;
            idx += 4;
        }
        while (idx < size) {
            h += (uint32_t)p[idx] * XXH32_PRIME5;
            h = xx_nchash_rotl32(h, 11) * XXH32_PRIME1;
            idx++;
        }
    }

    h ^= h >> 15;
    h *= XXH32_PRIME2;
    h ^= h >> 13;
    h *= XXH32_PRIME3;
    h ^= h >> 16;
    return h;
}

#define XXH64_PRIME1 0x9E3779B185EBCA87ULL
#define XXH64_PRIME2 0xC2B2AE3D27D4EB4FULL
#define XXH64_PRIME3 0x165667B19E3779F9ULL
#define XXH64_PRIME4 0x85EBCA77C2B2AE63ULL
#define XXH64_PRIME5 0x27D4EB2F165667C5ULL

static inline uint64_t xx_nchash_xxh64_round(uint64_t acc, uint64_t input)
{
    acc += input * XXH64_PRIME2;
    acc = xx_nchash_rotl64(acc, 31);
    acc *= XXH64_PRIME1;
    return acc;
}

static inline uint64_t xx_nchash_xxh64_merge_round(uint64_t acc, uint64_t val)
{
    val = xx_nchash_xxh64_round(0, val);
    acc ^= val;
    acc = acc * XXH64_PRIME1 + XXH64_PRIME4;
    return acc;
}

uint64_t xx_nchash_xxhash64(const void *data, size_t size, uint64_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h;

    if (size >= 32 && p) {
        uint64_t v1 = seed + XXH64_PRIME1 + XXH64_PRIME2;
        uint64_t v2 = seed + XXH64_PRIME2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - XXH64_PRIME1;

        size_t nblocks = size / 32;
        for (size_t i = 0; i < nblocks; ++i) {
            v1 = xx_nchash_xxh64_round(v1, xx_nchash_load64_le(p + i * 32));
            v2 = xx_nchash_xxh64_round(v2, xx_nchash_load64_le(p + i * 32 + 8));
            v3 = xx_nchash_xxh64_round(v3, xx_nchash_load64_le(p + i * 32 + 16));
            v4 = xx_nchash_xxh64_round(v4, xx_nchash_load64_le(p + i * 32 + 24));
        }
        h = xx_nchash_rotl64(v1, 1) + xx_nchash_rotl64(v2, 7) + xx_nchash_rotl64(v3, 12) + xx_nchash_rotl64(v4, 18);
        h = xx_nchash_xxh64_merge_round(h, v1);
        h = xx_nchash_xxh64_merge_round(h, v2);
        h = xx_nchash_xxh64_merge_round(h, v3);
        h = xx_nchash_xxh64_merge_round(h, v4);
    } else {
        h = seed + XXH64_PRIME5;
    }

    h += (uint64_t)size;
    size_t idx = (size >= 32) ? (size / 32) * 32 : 0;
    if (p) {
        while (idx + 8 <= size) {
            uint64_t k1 = xx_nchash_xxh64_round(0, xx_nchash_load64_le(p + idx));
            h ^= k1;
            h = xx_nchash_rotl64(h, 27) * XXH64_PRIME1 + XXH64_PRIME4;
            idx += 8;
        }
        if (idx + 4 <= size) {
            h ^= (uint64_t)xx_nchash_load32_le(p + idx) * XXH64_PRIME1;
            h = xx_nchash_rotl64(h, 23) * XXH64_PRIME2 + XXH64_PRIME3;
            idx += 4;
        }
        while (idx < size) {
            h ^= (uint64_t)p[idx] * XXH64_PRIME5;
            h = xx_nchash_rotl64(h, 11) * XXH64_PRIME1;
            idx++;
        }
    }

    h ^= h >> 33;
    h *= XXH64_PRIME2;
    h ^= h >> 29;
    h *= XXH64_PRIME3;
    h ^= h >> 32;
    return h;
}

/* ========================================================================= */
/* --- SipHash-2-4                                                       --- */
/* ========================================================================= */

#define SIPROUND(v0, v1, v2, v3)       \
    do {                               \
        v0 += v1;                      \
        v1 = xx_nchash_rotl64(v1, 13); \
        v1 ^= v0;                      \
        v0 = xx_nchash_rotl64(v0, 32); \
        v2 += v3;                      \
        v3 = xx_nchash_rotl64(v3, 16); \
        v3 ^= v2;                      \
        v0 += v3;                      \
        v3 = xx_nchash_rotl64(v3, 21); \
        v3 ^= v0;                      \
        v2 += v1;                      \
        v1 = xx_nchash_rotl64(v1, 17); \
        v1 ^= v2;                      \
        v2 = xx_nchash_rotl64(v2, 32); \
    } while (0)

static void xx_nchash_siphash_core(const void *data, size_t size, const uint8_t key[16], bool is128, uint64_t *out0, uint64_t *out1)
{
    uint64_t k0 = key ? xx_nchash_load64_le(key) : 0;
    uint64_t k1 = key ? xx_nchash_load64_le(key + 8) : 0;
    uint64_t v0 = k0 ^ 0x736F6D6570736575ULL;
    uint64_t v1 = k1 ^ 0x646F72616E646F6DULL;
    uint64_t v2 = k0 ^ 0x6C7967656E657261ULL;
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;

    if (is128) v1 ^= 0xEE;

    const uint8_t *p = (const uint8_t *)data;
    size_t nblocks = size / 8;
    if (p) {
        for (size_t i = 0; i < nblocks; ++i) {
            uint64_t m = xx_nchash_load64_le(p + i * 8);
            v3 ^= m;
            SIPROUND(v0, v1, v2, v3);
            SIPROUND(v0, v1, v2, v3);
            v0 ^= m;
        }

        const uint8_t *tail = p + nblocks * 8;
        uint64_t b = (uint64_t)size << 56;
        size_t rem = size & 7;
        for (size_t i = 0; i < rem; ++i) {
            b |= (uint64_t)tail[i] << (8 * i);
        }
        v3 ^= b;
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        v0 ^= b;
    } else {
        uint64_t b = (uint64_t)size << 56;
        v3 ^= b;
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        v0 ^= b;
    }

    if (!is128) {
        v2 ^= 0xFF;
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        *out0 = v0 ^ v1 ^ v2 ^ v3;
        *out1 = 0;
    } else {
        v2 ^= 0xEE;
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        *out0 = v0 ^ v1 ^ v2 ^ v3;
        v1 ^= 0xDD;
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        SIPROUND(v0, v1, v2, v3);
        *out1 = v0 ^ v1 ^ v2 ^ v3;
    }
}

uint64_t xx_nchash_siphash64(const void *data, size_t size, const uint8_t key[16])
{
    uint64_t o0, o1;
    xx_nchash_siphash_core(data, size, key, false, &o0, &o1);
    return o0;
}

void xx_nchash_siphash128(const void *data, size_t size, const uint8_t key[16], uint8_t out[16])
{
    uint64_t o0, o1;
    xx_nchash_siphash_core(data, size, key, true, &o0, &o1);
    if (out) {
        xx_nchash_store64_le(out, o0);
        xx_nchash_store64_le(out + 8, o1);
    }
}

/* ========================================================================= */
/* --- BLAKE2b-512 & BLAKE2s-256 (RFC 7693)                              --- */
/* ========================================================================= */

static const uint8_t xx_blake2_sigma[12][16] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
                                                {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4}, {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
                                                {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13}, {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
                                                {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11}, {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
                                                {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5}, {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
                                                {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};

static const uint64_t xx_blake2b_iv[8] = {0x6A09E667F3BCC908ULL, 0xBB67AE8584CAA73BULL, 0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL,
                                          0x510E527FADE682D1ULL, 0x9B05688C2B3E6C1FULL, 0x1F83D9ABFB41BD6BULL, 0x5BE0CD19137E2179ULL};

static const uint32_t xx_blake2s_iv[8] = {0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU, 0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};

#define B2B_G(a, b, c, d, x, y)          \
    do {                                 \
        a = a + b + x;                   \
        d = xx_nchash_rotr64(d ^ a, 32); \
        c = c + d;                       \
        b = xx_nchash_rotr64(b ^ c, 24); \
        a = a + b + y;                   \
        d = xx_nchash_rotr64(d ^ a, 16); \
        c = c + d;                       \
        b = xx_nchash_rotr64(b ^ c, 63); \
    } while (0)

static void xx_blake2b_compress(xx_nchash_blake2b_ctx *ctx, const uint8_t block[128])
{
    uint64_t m[16], v[16];
    for (size_t i = 0; i < 16; ++i) m[i] = xx_nchash_load64_le(block + i * 8);
    for (size_t i = 0; i < 8; ++i) v[i] = ctx->h[i];
    v[8] = xx_blake2b_iv[0];
    v[9] = xx_blake2b_iv[1];
    v[10] = xx_blake2b_iv[2];
    v[11] = xx_blake2b_iv[3];
    v[12] = xx_blake2b_iv[4] ^ ctx->t[0];
    v[13] = xx_blake2b_iv[5] ^ ctx->t[1];
    v[14] = xx_blake2b_iv[6] ^ ctx->f[0];
    v[15] = xx_blake2b_iv[7] ^ ctx->f[1];

    for (size_t r = 0; r < 12; ++r) {
        const uint8_t *s = xx_blake2_sigma[r];
        B2B_G(v[0], v[4], v[8], v[12], m[s[0]], m[s[1]]);
        B2B_G(v[1], v[5], v[9], v[13], m[s[2]], m[s[3]]);
        B2B_G(v[2], v[6], v[10], v[14], m[s[4]], m[s[5]]);
        B2B_G(v[3], v[7], v[11], v[15], m[s[6]], m[s[7]]);
        B2B_G(v[0], v[5], v[10], v[15], m[s[8]], m[s[9]]);
        B2B_G(v[1], v[6], v[11], v[12], m[s[10]], m[s[11]]);
        B2B_G(v[2], v[7], v[8], v[13], m[s[12]], m[s[13]]);
        B2B_G(v[3], v[4], v[9], v[14], m[s[14]], m[s[15]]);
    }

    for (size_t i = 0; i < 8; ++i) ctx->h[i] ^= v[i] ^ v[i + 8];
}

static void xx_blake2b_init(xx_nchash_blake2b_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    for (size_t i = 0; i < 8; ++i) ctx->h[i] = xx_blake2b_iv[i];
    ctx->h[0] ^= 0x01010040ULL; /* unkeyed, 64-byte digest */
}

static void xx_blake2b_update(xx_nchash_blake2b_ctx *ctx, const uint8_t *in, size_t inlen)
{
    while (inlen > 0) {
        if (ctx->buflen == 128) {
            ctx->t[0] += 128;
            if (ctx->t[0] < 128) ctx->t[1]++;
            xx_blake2b_compress(ctx, ctx->buf);
            ctx->buflen = 0;
        }
        size_t want = 128 - ctx->buflen;
        size_t take = inlen < want ? inlen : want;
        memcpy(ctx->buf + ctx->buflen, in, take);
        ctx->buflen += take;
        in += take;
        inlen -= take;
    }
}

static void xx_blake2b_final(xx_nchash_blake2b_ctx *ctx, uint8_t digest[64])
{
    ctx->t[0] += (uint64_t)ctx->buflen;
    if (ctx->t[0] < (uint64_t)ctx->buflen) ctx->t[1]++;
    ctx->f[0] = ~0ULL;
    memset(ctx->buf + ctx->buflen, 0, 128 - ctx->buflen);
    xx_blake2b_compress(ctx, ctx->buf);
    for (size_t i = 0; i < 8; ++i) {
        xx_nchash_store64_le(digest + i * 8, ctx->h[i]);
    }
}

bool xx_nchash_blake2b_512(const void *data, size_t size, uint8_t digest[64])
{
    if (!digest) return false;
    xx_nchash_blake2b_ctx ctx;
    xx_blake2b_init(&ctx);
    if (data && size) xx_blake2b_update(&ctx, (const uint8_t *)data, size);
    xx_blake2b_final(&ctx, digest);
    return true;
}

#define B2S_G(a, b, c, d, x, y)          \
    do {                                 \
        a = a + b + x;                   \
        d = xx_nchash_rotr32(d ^ a, 16); \
        c = c + d;                       \
        b = xx_nchash_rotr32(b ^ c, 12); \
        a = a + b + y;                   \
        d = xx_nchash_rotr32(d ^ a, 8);  \
        c = c + d;                       \
        b = xx_nchash_rotr32(b ^ c, 7);  \
    } while (0)

static void xx_blake2s_compress(xx_nchash_blake2s_ctx *ctx, const uint8_t block[64])
{
    uint32_t m[16], v[16];
    for (size_t i = 0; i < 16; ++i) m[i] = xx_nchash_load32_le(block + i * 4);
    for (size_t i = 0; i < 8; ++i) v[i] = ctx->h[i];
    v[8] = xx_blake2s_iv[0];
    v[9] = xx_blake2s_iv[1];
    v[10] = xx_blake2s_iv[2];
    v[11] = xx_blake2s_iv[3];
    v[12] = xx_blake2s_iv[4] ^ ctx->t[0];
    v[13] = xx_blake2s_iv[5] ^ ctx->t[1];
    v[14] = xx_blake2s_iv[6] ^ ctx->f[0];
    v[15] = xx_blake2s_iv[7] ^ ctx->f[1];

    for (size_t r = 0; r < 10; ++r) {
        const uint8_t *s = xx_blake2_sigma[r];
        B2S_G(v[0], v[4], v[8], v[12], m[s[0]], m[s[1]]);
        B2S_G(v[1], v[5], v[9], v[13], m[s[2]], m[s[3]]);
        B2S_G(v[2], v[6], v[10], v[14], m[s[4]], m[s[5]]);
        B2S_G(v[3], v[7], v[11], v[15], m[s[6]], m[s[7]]);
        B2S_G(v[0], v[5], v[10], v[15], m[s[8]], m[s[9]]);
        B2S_G(v[1], v[6], v[11], v[12], m[s[10]], m[s[11]]);
        B2S_G(v[2], v[7], v[8], v[13], m[s[12]], m[s[13]]);
        B2S_G(v[3], v[4], v[9], v[14], m[s[14]], m[s[15]]);
    }

    for (size_t i = 0; i < 8; ++i) ctx->h[i] ^= v[i] ^ v[i + 8];
}

static void xx_blake2s_init(xx_nchash_blake2s_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    for (size_t i = 0; i < 8; ++i) ctx->h[i] = xx_blake2s_iv[i];
    ctx->h[0] ^= 0x01010020U; /* unkeyed, 32-byte digest */
}

static void xx_blake2s_update(xx_nchash_blake2s_ctx *ctx, const uint8_t *in, size_t inlen)
{
    while (inlen > 0) {
        if (ctx->buflen == 64) {
            ctx->t[0] += 64;
            if (ctx->t[0] < 64) ctx->t[1]++;
            xx_blake2s_compress(ctx, ctx->buf);
            ctx->buflen = 0;
        }
        size_t want = 64 - ctx->buflen;
        size_t take = inlen < want ? inlen : want;
        memcpy(ctx->buf + ctx->buflen, in, take);
        ctx->buflen += take;
        in += take;
        inlen -= take;
    }
}

static void xx_blake2s_final(xx_nchash_blake2s_ctx *ctx, uint8_t digest[32])
{
    ctx->t[0] += (uint32_t)ctx->buflen;
    if (ctx->t[0] < (uint32_t)ctx->buflen) ctx->t[1]++;
    ctx->f[0] = ~0U;
    memset(ctx->buf + ctx->buflen, 0, 64 - ctx->buflen);
    xx_blake2s_compress(ctx, ctx->buf);
    for (size_t i = 0; i < 8; ++i) {
        xx_nchash_store32_le(digest + i * 4, ctx->h[i]);
    }
}

bool xx_nchash_blake2s_256(const void *data, size_t size, uint8_t digest[32])
{
    if (!digest) return false;
    xx_nchash_blake2s_ctx ctx;
    xx_blake2s_init(&ctx);
    if (data && size) xx_blake2s_update(&ctx, (const uint8_t *)data, size);
    xx_blake2s_final(&ctx, digest);
    return true;
}

/* ========================================================================= */
/* --- Unified Streaming API                                             --- */
/* ========================================================================= */

const char *xx_nchash_type_to_string(xx_nchash_type_t type)
{
    switch (type) {
        case XX_NCHASH_FNV1_32: return "FNV-1 (32-bit)";
        case XX_NCHASH_FNV1A_32: return "FNV-1a (32-bit)";
        case XX_NCHASH_FNV1_64: return "FNV-1 (64-bit)";
        case XX_NCHASH_FNV1A_64: return "FNV-1a (64-bit)";
        case XX_NCHASH_DJB2: return "DJB2";
        case XX_NCHASH_SDBM: return "SDBM";
        case XX_NCHASH_ELF: return "ELF";
        case XX_NCHASH_PJW: return "PJW";
        case XX_NCHASH_BKDR: return "BKDR";
        case XX_NCHASH_AP: return "AP";
        case XX_NCHASH_DEK: return "DEK";
        case XX_NCHASH_JS: return "JS";
        case XX_NCHASH_RS: return "RS";
        case XX_NCHASH_OAT: return "One-at-a-time";
        case XX_NCHASH_MURMUR3_32: return "MurmurHash3 (x86_32)";
        case XX_NCHASH_MURMUR3_128: return "MurmurHash3 (x64_128)";
        case XX_NCHASH_XXHASH32: return "xxHash32";
        case XX_NCHASH_XXHASH64: return "xxHash64";
        case XX_NCHASH_SIPHASH64: return "SipHash-2-4 (64-bit)";
        case XX_NCHASH_SIPHASH128: return "SipHash-2-4 (128-bit)";
        case XX_NCHASH_BLAKE2B_512: return "BLAKE2b-512";
        case XX_NCHASH_BLAKE2S_256: return "BLAKE2s-256";
        default: return "Unknown";
    }
}

xx_nchash_type_t xx_nchash_string_to_type(const char *name)
{
    if (!name) return XX_NCHASH_COUNT;
    char norm[64];
    size_t pos = 0;
    for (size_t i = 0; name[i] && pos + 1 < sizeof(norm); ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        if (c == '-' || c == '_' || c == ' ' || c == '/') continue;
        norm[pos++] = c;
    }
    norm[pos] = 0;

    if (!strcmp(norm, "fnv132") || !strcmp(norm, "fnv1")) return XX_NCHASH_FNV1_32;
    if (!strcmp(norm, "fnv1a32") || !strcmp(norm, "fnv1a")) return XX_NCHASH_FNV1A_32;
    if (!strcmp(norm, "fnv164")) return XX_NCHASH_FNV1_64;
    if (!strcmp(norm, "fnv1a64")) return XX_NCHASH_FNV1A_64;
    if (!strcmp(norm, "djb2")) return XX_NCHASH_DJB2;
    if (!strcmp(norm, "sdbm")) return XX_NCHASH_SDBM;
    if (!strcmp(norm, "elf")) return XX_NCHASH_ELF;
    if (!strcmp(norm, "pjw")) return XX_NCHASH_PJW;
    if (!strcmp(norm, "bkdr")) return XX_NCHASH_BKDR;
    if (!strcmp(norm, "ap")) return XX_NCHASH_AP;
    if (!strcmp(norm, "dek")) return XX_NCHASH_DEK;
    if (!strcmp(norm, "js")) return XX_NCHASH_JS;
    if (!strcmp(norm, "rs")) return XX_NCHASH_RS;
    if (!strcmp(norm, "oat") || !strcmp(norm, "oneatatime")) return XX_NCHASH_OAT;
    if (!strcmp(norm, "murmur332") || !strcmp(norm, "murmurhash332") || !strcmp(norm, "murmur3")) return XX_NCHASH_MURMUR3_32;
    if (!strcmp(norm, "murmur3128") || !strcmp(norm, "murmurhash3128")) return XX_NCHASH_MURMUR3_128;
    if (!strcmp(norm, "xxhash32") || !strcmp(norm, "xxh32")) return XX_NCHASH_XXHASH32;
    if (!strcmp(norm, "xxhash64") || !strcmp(norm, "xxh64") || !strcmp(norm, "xxhash")) return XX_NCHASH_XXHASH64;
    if (!strcmp(norm, "siphash64") || !strcmp(norm, "siphash2464") || !strcmp(norm, "siphash")) return XX_NCHASH_SIPHASH64;
    if (!strcmp(norm, "siphash128") || !strcmp(norm, "siphash24128")) return XX_NCHASH_SIPHASH128;
    if (!strcmp(norm, "blake2b512") || !strcmp(norm, "blake2b")) return XX_NCHASH_BLAKE2B_512;
    if (!strcmp(norm, "blake2s256") || !strcmp(norm, "blake2s")) return XX_NCHASH_BLAKE2S_256;
    return XX_NCHASH_COUNT;
}

size_t xx_nchash_digest_size(xx_nchash_type_t type)
{
    switch (type) {
        case XX_NCHASH_FNV1_32:
        case XX_NCHASH_FNV1A_32:
        case XX_NCHASH_DJB2:
        case XX_NCHASH_SDBM:
        case XX_NCHASH_ELF:
        case XX_NCHASH_PJW:
        case XX_NCHASH_BKDR:
        case XX_NCHASH_AP:
        case XX_NCHASH_DEK:
        case XX_NCHASH_JS:
        case XX_NCHASH_RS:
        case XX_NCHASH_OAT:
        case XX_NCHASH_MURMUR3_32:
        case XX_NCHASH_XXHASH32: return 4;
        case XX_NCHASH_FNV1_64:
        case XX_NCHASH_FNV1A_64:
        case XX_NCHASH_XXHASH64:
        case XX_NCHASH_SIPHASH64: return 8;
        case XX_NCHASH_MURMUR3_128:
        case XX_NCHASH_SIPHASH128: return 16;
        case XX_NCHASH_BLAKE2S_256: return 32;
        case XX_NCHASH_BLAKE2B_512: return 64;
        default: return 0;
    }
}

bool xx_nchash_context_init(xx_nchash_context *ctx, xx_nchash_type_t type)
{
    if (!ctx || type >= XX_NCHASH_COUNT) return false;
    memset(ctx, 0, sizeof(*ctx));
    ctx->type = type;
    ctx->initialized = true;

    switch (type) {
        case XX_NCHASH_FNV1_32: ctx->u.fnv1_32.state = FNV1_32_INIT; break;
        case XX_NCHASH_FNV1A_32: ctx->u.fnv1a_32.state = FNV1_32_INIT; break;
        case XX_NCHASH_FNV1_64: ctx->u.fnv1_64.state = FNV1_64_INIT; break;
        case XX_NCHASH_FNV1A_64: ctx->u.fnv1a_64.state = FNV1_64_INIT; break;
        case XX_NCHASH_DJB2: ctx->u.djb2.state = 5381U; break;
        case XX_NCHASH_SDBM: ctx->u.sdbm.state = 0U; break;
        case XX_NCHASH_ELF: ctx->u.elf.state = 0U; break;
        case XX_NCHASH_PJW: ctx->u.pjw.state = 0U; break;
        case XX_NCHASH_BKDR: ctx->u.bkdr.state = 0U; break;
        case XX_NCHASH_AP:
            ctx->u.ap.state = 0xAAAAAAAAU;
            ctx->u.ap.index = 0;
            break;
        case XX_NCHASH_DEK:
            ctx->u.dek.state = 0U;
            ctx->u.dek.length = 0;
            break;
        case XX_NCHASH_JS: ctx->u.js.state = 1315423911U; break;
        case XX_NCHASH_RS:
            ctx->u.rs.state = 0U;
            ctx->u.rs.a = 63689U;
            break;
        case XX_NCHASH_OAT: ctx->u.oat.state = 0U; break;
        case XX_NCHASH_MURMUR3_32: ctx->u.murmur3_32.h = 0U; break;
        case XX_NCHASH_MURMUR3_128:
            ctx->u.murmur3_128.h1 = 0ULL;
            ctx->u.murmur3_128.h2 = 0ULL;
            break;
        case XX_NCHASH_XXHASH32:
            ctx->u.xxhash32.seed = 0U;
            ctx->u.xxhash32.v1 = XXH32_PRIME1 + XXH32_PRIME2;
            ctx->u.xxhash32.v2 = XXH32_PRIME2;
            ctx->u.xxhash32.v3 = 0U;
            ctx->u.xxhash32.v4 = 0U - XXH32_PRIME1;
            break;
        case XX_NCHASH_XXHASH64:
            ctx->u.xxhash64.seed = 0ULL;
            ctx->u.xxhash64.v1 = XXH64_PRIME1 + XXH64_PRIME2;
            ctx->u.xxhash64.v2 = XXH64_PRIME2;
            ctx->u.xxhash64.v3 = 0ULL;
            ctx->u.xxhash64.v4 = 0ULL - XXH64_PRIME1;
            break;
        case XX_NCHASH_SIPHASH64:
            ctx->u.siphash.is128 = false;
            xx_nchash_context_set_key(ctx, NULL);
            break;
        case XX_NCHASH_SIPHASH128:
            ctx->u.siphash.is128 = true;
            xx_nchash_context_set_key(ctx, NULL);
            break;
        case XX_NCHASH_BLAKE2B_512: xx_blake2b_init(&ctx->u.blake2b); break;
        case XX_NCHASH_BLAKE2S_256: xx_blake2s_init(&ctx->u.blake2s); break;
        default: break;
    }
    return true;
}

void xx_nchash_context_set_seed(xx_nchash_context *ctx, uint64_t seed)
{
    if (!ctx) return;
    if (ctx->type == XX_NCHASH_MURMUR3_32) {
        ctx->u.murmur3_32.h = (uint32_t)seed;
    } else if (ctx->type == XX_NCHASH_MURMUR3_128) {
        ctx->u.murmur3_128.h1 = seed;
        ctx->u.murmur3_128.h2 = seed;
    } else if (ctx->type == XX_NCHASH_XXHASH32) {
        ctx->u.xxhash32.seed = (uint32_t)seed;
        ctx->u.xxhash32.v1 = (uint32_t)seed + XXH32_PRIME1 + XXH32_PRIME2;
        ctx->u.xxhash32.v2 = (uint32_t)seed + XXH32_PRIME2;
        ctx->u.xxhash32.v3 = (uint32_t)seed;
        ctx->u.xxhash32.v4 = (uint32_t)seed - XXH32_PRIME1;
    } else if (ctx->type == XX_NCHASH_XXHASH64) {
        ctx->u.xxhash64.seed = seed;
        ctx->u.xxhash64.v1 = seed + XXH64_PRIME1 + XXH64_PRIME2;
        ctx->u.xxhash64.v2 = seed + XXH64_PRIME2;
        ctx->u.xxhash64.v3 = seed;
        ctx->u.xxhash64.v4 = seed - XXH64_PRIME1;
    }
}

void xx_nchash_context_set_key(xx_nchash_context *ctx, const uint8_t key[16])
{
    if (!ctx) return;
    if (ctx->type == XX_NCHASH_SIPHASH64 || ctx->type == XX_NCHASH_SIPHASH128) {
        uint64_t k0 = key ? xx_nchash_load64_le(key) : 0;
        uint64_t k1 = key ? xx_nchash_load64_le(key + 8) : 0;
        ctx->u.siphash.v0 = k0 ^ 0x736F6D6570736575ULL;
        ctx->u.siphash.v1 = k1 ^ 0x646F72616E646F6DULL;
        ctx->u.siphash.v2 = k0 ^ 0x6C7967656E657261ULL;
        ctx->u.siphash.v3 = k1 ^ 0x7465646279746573ULL;
        if (ctx->u.siphash.is128) ctx->u.siphash.v1 ^= 0xEE;
    }
}

void xx_nchash_context_update(xx_nchash_context *ctx, const void *data, size_t size)
{
    if (!ctx || !ctx->initialized || !data || !size) return;
    const uint8_t *p = (const uint8_t *)data;
    ctx->total_size += size;

    switch (ctx->type) {
        case XX_NCHASH_FNV1_32:
            for (size_t i = 0; i < size; ++i) ctx->u.fnv1_32.state = (ctx->u.fnv1_32.state * FNV1_32_PRIME) ^ p[i];
            break;
        case XX_NCHASH_FNV1A_32:
            for (size_t i = 0; i < size; ++i) ctx->u.fnv1a_32.state = (ctx->u.fnv1a_32.state ^ p[i]) * FNV1_32_PRIME;
            break;
        case XX_NCHASH_FNV1_64:
            for (size_t i = 0; i < size; ++i) ctx->u.fnv1_64.state = (ctx->u.fnv1_64.state * FNV1_64_PRIME) ^ p[i];
            break;
        case XX_NCHASH_FNV1A_64:
            for (size_t i = 0; i < size; ++i) ctx->u.fnv1a_64.state = (ctx->u.fnv1a_64.state ^ p[i]) * FNV1_64_PRIME;
            break;
        case XX_NCHASH_DJB2:
            for (size_t i = 0; i < size; ++i) ctx->u.djb2.state = ((ctx->u.djb2.state << 5) + ctx->u.djb2.state) + p[i];
            break;
        case XX_NCHASH_SDBM:
            for (size_t i = 0; i < size; ++i) ctx->u.sdbm.state = p[i] + (ctx->u.sdbm.state << 6) + (ctx->u.sdbm.state << 16) - ctx->u.sdbm.state;
            break;
        case XX_NCHASH_ELF:
        case XX_NCHASH_PJW:
            for (size_t i = 0; i < size; ++i) {
                ctx->u.elf.state = (ctx->u.elf.state << 4) + p[i];
                uint32_t g = ctx->u.elf.state & 0xF0000000U;
                if (g) ctx->u.elf.state ^= (g >> 24);
                ctx->u.elf.state &= ~g;
            }
            break;
        case XX_NCHASH_BKDR:
            for (size_t i = 0; i < size; ++i) ctx->u.bkdr.state = ctx->u.bkdr.state * 131U + p[i];
            break;
        case XX_NCHASH_AP:
            for (size_t i = 0; i < size; ++i) {
                if ((ctx->u.ap.index & 1) == 0) {
                    ctx->u.ap.state ^= ((ctx->u.ap.state << 7) ^ p[i] ^ (ctx->u.ap.state >> 3));
                } else {
                    ctx->u.ap.state ^= (~((ctx->u.ap.state << 11) ^ p[i] ^ (ctx->u.ap.state >> 5)));
                }
                ctx->u.ap.index++;
            }
            break;
        case XX_NCHASH_DEK:
            for (size_t i = 0; i < size; ++i) {
                ctx->u.dek.state = xx_nchash_rotl32(ctx->u.dek.state, 5) ^ p[i];
                ctx->u.dek.length++;
            }
            break;
        case XX_NCHASH_JS:
            for (size_t i = 0; i < size; ++i) ctx->u.js.state ^= ((ctx->u.js.state << 5) + p[i] + (ctx->u.js.state >> 2));
            break;
        case XX_NCHASH_RS:
            for (size_t i = 0; i < size; ++i) {
                ctx->u.rs.state = ctx->u.rs.state * ctx->u.rs.a + p[i];
                ctx->u.rs.a *= 378551U;
            }
            break;
        case XX_NCHASH_OAT:
            for (size_t i = 0; i < size; ++i) {
                ctx->u.oat.state += p[i];
                ctx->u.oat.state += (ctx->u.oat.state << 10);
                ctx->u.oat.state ^= (ctx->u.oat.state >> 6);
            }
            break;
        case XX_NCHASH_MURMUR3_32: {
            xx_nchash_murmur3_32_ctx *m = &ctx->u.murmur3_32;
            const uint32_t c1 = 0xCC9E2D51U, c2 = 0x1B873593U;
            size_t offset = 0;
            if (m->buffered) {
                while (m->buffered < 4 && offset < size) {
                    m->buffer[m->buffered++] = p[offset++];
                }
                if (m->buffered == 4) {
                    uint32_t k = xx_nchash_load32_le(m->buffer);
                    k *= c1;
                    k = xx_nchash_rotl32(k, 15);
                    k *= c2;
                    m->h ^= k;
                    m->h = xx_nchash_rotl32(m->h, 13);
                    m->h = m->h * 5U + 0xE6546B64U;
                    m->buffered = 0;
                }
            }
            while (offset + 4 <= size) {
                uint32_t k = xx_nchash_load32_le(p + offset);
                k *= c1;
                k = xx_nchash_rotl32(k, 15);
                k *= c2;
                m->h ^= k;
                m->h = xx_nchash_rotl32(m->h, 13);
                m->h = m->h * 5U + 0xE6546B64U;
                offset += 4;
            }
            while (offset < size) {
                m->buffer[m->buffered++] = p[offset++];
            }
            break;
        }
        case XX_NCHASH_MURMUR3_128: {
            xx_nchash_murmur3_128_ctx *m = &ctx->u.murmur3_128;
            const uint64_t c1 = 0x87C37B91114253D5ULL, c2 = 0x4CF5AD4327459204ULL;
            size_t offset = 0;
            if (m->buffered) {
                while (m->buffered < 16 && offset < size) {
                    m->buffer[m->buffered++] = p[offset++];
                }
                if (m->buffered == 16) {
                    uint64_t k1 = xx_nchash_load64_le(m->buffer);
                    uint64_t k2 = xx_nchash_load64_le(m->buffer + 8);
                    k1 *= c1;
                    k1 = xx_nchash_rotl64(k1, 31);
                    k1 *= c2;
                    m->h1 ^= k1;
                    m->h1 = xx_nchash_rotl64(m->h1, 27);
                    m->h1 += m->h2;
                    m->h1 = m->h1 * 5ULL + 0x52DCE729ULL;
                    k2 *= c2;
                    k2 = xx_nchash_rotl64(k2, 33);
                    k2 *= c1;
                    m->h2 ^= k2;
                    m->h2 = xx_nchash_rotl64(m->h2, 31);
                    m->h2 += m->h1;
                    m->h2 = m->h2 * 5ULL + 0x38495AB5ULL;
                    m->buffered = 0;
                }
            }
            while (offset + 16 <= size) {
                uint64_t k1 = xx_nchash_load64_le(p + offset);
                uint64_t k2 = xx_nchash_load64_le(p + offset + 8);
                k1 *= c1;
                k1 = xx_nchash_rotl64(k1, 31);
                k1 *= c2;
                m->h1 ^= k1;
                m->h1 = xx_nchash_rotl64(m->h1, 27);
                m->h1 += m->h2;
                m->h1 = m->h1 * 5ULL + 0x52DCE729ULL;
                k2 *= c2;
                k2 = xx_nchash_rotl64(k2, 33);
                k2 *= c1;
                m->h2 ^= k2;
                m->h2 = xx_nchash_rotl64(m->h2, 31);
                m->h2 += m->h1;
                m->h2 = m->h2 * 5ULL + 0x38495AB5ULL;
                offset += 16;
            }
            while (offset < size) {
                m->buffer[m->buffered++] = p[offset++];
            }
            break;
        }
        case XX_NCHASH_XXHASH32: {
            xx_nchash_xxhash32_ctx *x = &ctx->u.xxhash32;
            size_t offset = 0;
            if (x->buffered) {
                while (x->buffered < 16 && offset < size) {
                    x->buffer[x->buffered++] = p[offset++];
                }
                if (x->buffered == 16) {
                    x->v1 = xx_nchash_rotl32(x->v1 + xx_nchash_load32_le(x->buffer) * XXH32_PRIME2, 13) * XXH32_PRIME1;
                    x->v2 = xx_nchash_rotl32(x->v2 + xx_nchash_load32_le(x->buffer + 4) * XXH32_PRIME2, 13) * XXH32_PRIME1;
                    x->v3 = xx_nchash_rotl32(x->v3 + xx_nchash_load32_le(x->buffer + 8) * XXH32_PRIME2, 13) * XXH32_PRIME1;
                    x->v4 = xx_nchash_rotl32(x->v4 + xx_nchash_load32_le(x->buffer + 12) * XXH32_PRIME2, 13) * XXH32_PRIME1;
                    x->buffered = 0;
                }
            }
            while (offset + 16 <= size) {
                x->v1 = xx_nchash_rotl32(x->v1 + xx_nchash_load32_le(p + offset) * XXH32_PRIME2, 13) * XXH32_PRIME1;
                x->v2 = xx_nchash_rotl32(x->v2 + xx_nchash_load32_le(p + offset + 4) * XXH32_PRIME2, 13) * XXH32_PRIME1;
                x->v3 = xx_nchash_rotl32(x->v3 + xx_nchash_load32_le(p + offset + 8) * XXH32_PRIME2, 13) * XXH32_PRIME1;
                x->v4 = xx_nchash_rotl32(x->v4 + xx_nchash_load32_le(p + offset + 12) * XXH32_PRIME2, 13) * XXH32_PRIME1;
                offset += 16;
            }
            while (offset < size) {
                x->buffer[x->buffered++] = p[offset++];
            }
            break;
        }
        case XX_NCHASH_XXHASH64: {
            xx_nchash_xxhash64_ctx *x = &ctx->u.xxhash64;
            size_t offset = 0;
            if (x->buffered) {
                while (x->buffered < 32 && offset < size) {
                    x->buffer[x->buffered++] = p[offset++];
                }
                if (x->buffered == 32) {
                    x->v1 = xx_nchash_xxh64_round(x->v1, xx_nchash_load64_le(x->buffer));
                    x->v2 = xx_nchash_xxh64_round(x->v2, xx_nchash_load64_le(x->buffer + 8));
                    x->v3 = xx_nchash_xxh64_round(x->v3, xx_nchash_load64_le(x->buffer + 16));
                    x->v4 = xx_nchash_xxh64_round(x->v4, xx_nchash_load64_le(x->buffer + 24));
                    x->buffered = 0;
                }
            }
            while (offset + 32 <= size) {
                x->v1 = xx_nchash_xxh64_round(x->v1, xx_nchash_load64_le(p + offset));
                x->v2 = xx_nchash_xxh64_round(x->v2, xx_nchash_load64_le(p + offset + 8));
                x->v3 = xx_nchash_xxh64_round(x->v3, xx_nchash_load64_le(p + offset + 16));
                x->v4 = xx_nchash_xxh64_round(x->v4, xx_nchash_load64_le(p + offset + 24));
                offset += 32;
            }
            while (offset < size) {
                x->buffer[x->buffered++] = p[offset++];
            }
            break;
        }
        case XX_NCHASH_SIPHASH64:
        case XX_NCHASH_SIPHASH128: {
            xx_nchash_siphash_ctx *s = &ctx->u.siphash;
            size_t offset = 0;
            if (s->buffered) {
                while (s->buffered < 8 && offset < size) {
                    s->buffer[s->buffered++] = p[offset++];
                }
                if (s->buffered == 8) {
                    uint64_t m = xx_nchash_load64_le(s->buffer);
                    s->v3 ^= m;
                    SIPROUND(s->v0, s->v1, s->v2, s->v3);
                    SIPROUND(s->v0, s->v1, s->v2, s->v3);
                    s->v0 ^= m;
                    s->buffered = 0;
                }
            }
            while (offset + 8 <= size) {
                uint64_t m = xx_nchash_load64_le(p + offset);
                s->v3 ^= m;
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                s->v0 ^= m;
                offset += 8;
            }
            while (offset < size) {
                s->buffer[s->buffered++] = p[offset++];
            }
            break;
        }
        case XX_NCHASH_BLAKE2B_512: xx_blake2b_update(&ctx->u.blake2b, p, size); break;
        case XX_NCHASH_BLAKE2S_256: xx_blake2s_update(&ctx->u.blake2s, p, size); break;
        default: break;
    }
}

bool xx_nchash_context_final(xx_nchash_context *ctx, void *digest, size_t digest_size)
{
    if (!ctx || !ctx->initialized || !digest) return false;
    size_t required = xx_nchash_digest_size(ctx->type);
    if (digest_size < required) return false;

    uint8_t *out = (uint8_t *)digest;

    switch (ctx->type) {
        case XX_NCHASH_FNV1_32: xx_nchash_store32_be(out, ctx->u.fnv1_32.state); break;
        case XX_NCHASH_FNV1A_32: xx_nchash_store32_be(out, ctx->u.fnv1a_32.state); break;
        case XX_NCHASH_FNV1_64: xx_nchash_store64_be(out, ctx->u.fnv1_64.state); break;
        case XX_NCHASH_FNV1A_64: xx_nchash_store64_be(out, ctx->u.fnv1a_64.state); break;
        case XX_NCHASH_DJB2: xx_nchash_store32_be(out, ctx->u.djb2.state); break;
        case XX_NCHASH_SDBM: xx_nchash_store32_be(out, ctx->u.sdbm.state); break;
        case XX_NCHASH_ELF:
        case XX_NCHASH_PJW: xx_nchash_store32_be(out, ctx->u.elf.state); break;
        case XX_NCHASH_BKDR: xx_nchash_store32_be(out, ctx->u.bkdr.state); break;
        case XX_NCHASH_AP: xx_nchash_store32_be(out, ctx->u.ap.state); break;
        case XX_NCHASH_DEK: {
            uint32_t len = (uint32_t)ctx->u.dek.length;
            uint32_t final_h = ctx->u.dek.state ^ xx_nchash_rotl32(len, (5U * (len % 32U)) % 32U);
            xx_nchash_store32_be(out, final_h);
            break;
        }
        case XX_NCHASH_JS: xx_nchash_store32_be(out, ctx->u.js.state); break;
        case XX_NCHASH_RS: xx_nchash_store32_be(out, ctx->u.rs.state); break;
        case XX_NCHASH_OAT: {
            uint32_t h = ctx->u.oat.state;
            h += (h << 3);
            h ^= (h >> 11);
            h += (h << 15);
            xx_nchash_store32_be(out, h);
            break;
        }
        case XX_NCHASH_MURMUR3_32: {
            xx_nchash_murmur3_32_ctx *m = &ctx->u.murmur3_32;
            const uint32_t c1 = 0xCC9E2D51U, c2 = 0x1B873593U;
            uint32_t k1 = 0;
            switch (m->buffered) {
                case 3: k1 ^= (uint32_t)m->buffer[2] << 16; /* fallthrough */
                case 2: k1 ^= (uint32_t)m->buffer[1] << 8;  /* fallthrough */
                case 1:
                    k1 ^= (uint32_t)m->buffer[0];
                    k1 *= c1;
                    k1 = xx_nchash_rotl32(k1, 15);
                    k1 *= c2;
                    m->h ^= k1;
                    break;
                default: break;
            }
            m->h ^= (uint32_t)ctx->total_size;
            m->h = xx_nchash_fmix32(m->h);
            xx_nchash_store32_be(out, m->h);
            break;
        }
        case XX_NCHASH_MURMUR3_128: {
            xx_nchash_murmur3_128_ctx *m = &ctx->u.murmur3_128;
            const uint64_t c1 = 0x87C37B91114253D5ULL, c2 = 0x4CF5AD4327459204ULL;
            uint64_t k1 = 0, k2 = 0;
            switch (m->buffered) {
                case 15: k2 ^= (uint64_t)m->buffer[14] << 48; /* fallthrough */
                case 14: k2 ^= (uint64_t)m->buffer[13] << 40; /* fallthrough */
                case 13: k2 ^= (uint64_t)m->buffer[12] << 32; /* fallthrough */
                case 12: k2 ^= (uint64_t)m->buffer[11] << 24; /* fallthrough */
                case 11: k2 ^= (uint64_t)m->buffer[10] << 16; /* fallthrough */
                case 10: k2 ^= (uint64_t)m->buffer[9] << 8;   /* fallthrough */
                case 9:
                    k2 ^= (uint64_t)m->buffer[8];
                    k2 *= c2;
                    k2 = xx_nchash_rotl64(k2, 33);
                    k2 *= c1;
                    m->h2 ^= k2;
                    /* fallthrough */
                case 8: k1 ^= (uint64_t)m->buffer[7] << 56; /* fallthrough */
                case 7: k1 ^= (uint64_t)m->buffer[6] << 48; /* fallthrough */
                case 6: k1 ^= (uint64_t)m->buffer[5] << 40; /* fallthrough */
                case 5: k1 ^= (uint64_t)m->buffer[4] << 32; /* fallthrough */
                case 4: k1 ^= (uint64_t)m->buffer[3] << 24; /* fallthrough */
                case 3: k1 ^= (uint64_t)m->buffer[2] << 16; /* fallthrough */
                case 2: k1 ^= (uint64_t)m->buffer[1] << 8;  /* fallthrough */
                case 1:
                    k1 ^= (uint64_t)m->buffer[0];
                    k1 *= c1;
                    k1 = xx_nchash_rotl64(k1, 31);
                    k1 *= c2;
                    m->h1 ^= k1;
                    break;
                default: break;
            }
            m->h1 ^= ctx->total_size;
            m->h2 ^= ctx->total_size;
            m->h1 += m->h2;
            m->h2 += m->h1;
            m->h1 = xx_nchash_fmix64(m->h1);
            m->h2 = xx_nchash_fmix64(m->h2);
            m->h1 += m->h2;
            m->h2 += m->h1;
            xx_nchash_store64_le(out, m->h1);
            xx_nchash_store64_le(out + 8, m->h2);
            break;
        }
        case XX_NCHASH_XXHASH32: {
            xx_nchash_xxhash32_ctx *x = &ctx->u.xxhash32;
            uint32_t h;
            if (ctx->total_size >= 16) {
                h = xx_nchash_rotl32(x->v1, 1) + xx_nchash_rotl32(x->v2, 7) + xx_nchash_rotl32(x->v3, 12) + xx_nchash_rotl32(x->v4, 18);
            } else {
                h = x->seed + XXH32_PRIME5;
            }
            h += (uint32_t)ctx->total_size;
            size_t idx = 0;
            while (idx + 4 <= x->buffered) {
                h += xx_nchash_load32_le(x->buffer + idx) * XXH32_PRIME3;
                h = xx_nchash_rotl32(h, 17) * XXH32_PRIME4;
                idx += 4;
            }
            while (idx < x->buffered) {
                h += (uint32_t)x->buffer[idx] * XXH32_PRIME5;
                h = xx_nchash_rotl32(h, 11) * XXH32_PRIME1;
                idx++;
            }
            h ^= h >> 15;
            h *= XXH32_PRIME2;
            h ^= h >> 13;
            h *= XXH32_PRIME3;
            h ^= h >> 16;
            xx_nchash_store32_be(out, h);
            break;
        }
        case XX_NCHASH_XXHASH64: {
            xx_nchash_xxhash64_ctx *x = &ctx->u.xxhash64;
            uint64_t h;
            if (ctx->total_size >= 32) {
                h = xx_nchash_rotl64(x->v1, 1) + xx_nchash_rotl64(x->v2, 7) + xx_nchash_rotl64(x->v3, 12) + xx_nchash_rotl64(x->v4, 18);
                h = xx_nchash_xxh64_merge_round(h, x->v1);
                h = xx_nchash_xxh64_merge_round(h, x->v2);
                h = xx_nchash_xxh64_merge_round(h, x->v3);
                h = xx_nchash_xxh64_merge_round(h, x->v4);
            } else {
                h = x->seed + XXH64_PRIME5;
            }
            h += ctx->total_size;
            size_t idx = 0;
            while (idx + 8 <= x->buffered) {
                uint64_t k1 = xx_nchash_xxh64_round(0, xx_nchash_load64_le(x->buffer + idx));
                h ^= k1;
                h = xx_nchash_rotl64(h, 27) * XXH64_PRIME1 + XXH64_PRIME4;
                idx += 8;
            }
            if (idx + 4 <= x->buffered) {
                h ^= (uint64_t)xx_nchash_load32_le(x->buffer + idx) * XXH64_PRIME1;
                h = xx_nchash_rotl64(h, 23) * XXH64_PRIME2 + XXH64_PRIME3;
                idx += 4;
            }
            while (idx < x->buffered) {
                h ^= (uint64_t)x->buffer[idx] * XXH64_PRIME5;
                h = xx_nchash_rotl64(h, 11) * XXH64_PRIME1;
                idx++;
            }
            h ^= h >> 33;
            h *= XXH64_PRIME2;
            h ^= h >> 29;
            h *= XXH64_PRIME3;
            h ^= h >> 32;
            xx_nchash_store64_be(out, h);
            break;
        }
        case XX_NCHASH_SIPHASH64:
        case XX_NCHASH_SIPHASH128: {
            xx_nchash_siphash_ctx *s = &ctx->u.siphash;
            uint64_t b = ctx->total_size << 56;
            for (size_t i = 0; i < s->buffered; ++i) {
                b |= (uint64_t)s->buffer[i] << (8 * i);
            }
            s->v3 ^= b;
            SIPROUND(s->v0, s->v1, s->v2, s->v3);
            SIPROUND(s->v0, s->v1, s->v2, s->v3);
            s->v0 ^= b;

            if (!s->is128) {
                s->v2 ^= 0xFF;
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                uint64_t res = s->v0 ^ s->v1 ^ s->v2 ^ s->v3;
                xx_nchash_store64_le(out, res);
            } else {
                s->v2 ^= 0xEE;
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                uint64_t out0 = s->v0 ^ s->v1 ^ s->v2 ^ s->v3;
                s->v1 ^= 0xDD;
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                SIPROUND(s->v0, s->v1, s->v2, s->v3);
                uint64_t out1 = s->v0 ^ s->v1 ^ s->v2 ^ s->v3;
                xx_nchash_store64_le(out, out0);
                xx_nchash_store64_le(out + 8, out1);
            }
            break;
        }
        case XX_NCHASH_BLAKE2B_512: xx_blake2b_final(&ctx->u.blake2b, out); break;
        case XX_NCHASH_BLAKE2S_256: xx_blake2s_final(&ctx->u.blake2s, out); break;
        default: return false;
    }

    return true;
}

/* ========================================================================= */
/* --- Device Helper                                                     --- */
/* ========================================================================= */

bool xx_nchash_device(xx_io_device *device, int64_t offset, int64_t size, xx_nchash_type_t type, const uint8_t *key_or_null, uint64_t seed, void *digest,
                      size_t digest_size, xx_pd_struct *pd)
{
    if (!device || !digest || type >= XX_NCHASH_COUNT) return false;
    int64_t dev_size = xx_io_total_size(device);
    if (offset < 0) offset = 0;
    if (dev_size >= 0 && offset > dev_size) return false;

    int64_t total = size;
    if (total < 0) {
        if (dev_size < offset) return false;
        total = dev_size - offset;
    } else if (dev_size >= 0 && total > dev_size - offset) {
        return false;
    }

    if (xx_io_seek64(device, offset, SEEK_SET) != 0) return false;

    xx_nchash_context ctx;
    if (!xx_nchash_context_init(&ctx, type)) return false;
    if (seed) xx_nchash_context_set_seed(&ctx, seed);
    if (key_or_null) xx_nchash_context_set_key(&ctx, key_or_null);

    const size_t chunk_size = 65536;
    uint8_t buffer[65536];
    int64_t remaining = total;
    int pd_level = -1;

    if (pd) {
        pd_level = xx_pd_enter_level(pd, (uint64_t)total, "Calculating Hash");
    }

    while (remaining > 0) {
        if (pd && xx_pd_is_stopped(pd)) {
            if (pd_level >= 0) xx_pd_leave_level(pd, pd_level);
            return false;
        }
        size_t to_read = (uint64_t)remaining < chunk_size ? (size_t)remaining : chunk_size;
        ssize_t n = xx_io_read(device, buffer, to_read);
        if (n <= 0 || (size_t)n > to_read) {
            if (pd_level >= 0) xx_pd_leave_level(pd, pd_level);
            return false;
        }
        xx_nchash_context_update(&ctx, buffer, (size_t)n);
        remaining -= n;
        if (pd && pd_level >= 0) xx_pd_increment_current(pd, pd_level, (uint64_t)n);
    }

    if (pd_level >= 0) xx_pd_leave_level(pd, pd_level);
    return xx_nchash_context_final(&ctx, digest, digest_size);
}
