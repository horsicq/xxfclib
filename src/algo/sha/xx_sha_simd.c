/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* SSE2 computes four consecutive message-schedule words in parallel. SHA
 * extensions additionally accelerate the compression rounds when present.
 * These are single-message backends: blocks retain their chaining order.
 *
 * SHA instruction lane conventions follow Intel's instruction definitions:
 * https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sha-extensions.html
 * The implementation is native C; no external cryptographic library is used.
 */

#include "xx_sha_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "platforms/xx_sha_platform.h"

#ifdef XX_SHA_X86

static const uint32_t xx_sha256_round_constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static XX_SHA_INLINE uint32_t xx_sha_rotl(uint32_t value, unsigned bits) {
    return (value << bits) | (value >> (32U - bits));
}

static XX_SHA_INLINE uint32_t xx_sha_rotr(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

XX_SHA_TARGET_SSE2
static XX_SHA_INLINE __m128i xx_sha_big_endian(__m128i value) {
    value = _mm_or_si128(_mm_slli_epi16(value, 8), _mm_srli_epi16(value, 8));
    value = _mm_shufflelo_epi16(value, _MM_SHUFFLE(2, 3, 0, 1));
    return _mm_shufflehi_epi16(value, _MM_SHUFFLE(2, 3, 0, 1));
}

XX_SHA_TARGET_SSE2
static XX_SHA_INLINE __m128i xx_sha_rol1(__m128i value) {
    return _mm_or_si128(_mm_slli_epi32(value, 1), _mm_srli_epi32(value, 31));
}

XX_SHA_TARGET_SSE2
static XX_SHA_INLINE __m128i xx_sha256_sigma0(__m128i value) {
    __m128i r7 = _mm_or_si128(_mm_srli_epi32(value, 7), _mm_slli_epi32(value, 25));
    __m128i r18 = _mm_or_si128(_mm_srli_epi32(value, 18), _mm_slli_epi32(value, 14));
    return _mm_xor_si128(_mm_xor_si128(r7, r18), _mm_srli_epi32(value, 3));
}

XX_SHA_TARGET_SSE2
static XX_SHA_INLINE __m128i xx_sha256_sigma1(__m128i value) {
    __m128i r17 = _mm_or_si128(_mm_srli_epi32(value, 17), _mm_slli_epi32(value, 15));
    __m128i r19 = _mm_or_si128(_mm_srli_epi32(value, 19), _mm_slli_epi32(value, 13));
    return _mm_xor_si128(_mm_xor_si128(r17, r19), _mm_srli_epi32(value, 10));
}

XX_SHA_TARGET_SSE2
static void xx_sha1_schedule_sse2(const uint8_t *block, uint32_t words[80]) {
    unsigned t;
    for (t = 0; t < 16; t += 4) {
        __m128i value = _mm_loadu_si128((const __m128i *)(const void *)(block + 4U * t));
        _mm_storeu_si128((__m128i *)(void *)(words + t), xx_sha_big_endian(value));
    }
    for (t = 16; t < 80; t += 4) {
        __m128i m16 = _mm_loadu_si128((const __m128i *)(const void *)(words + t - 16));
        __m128i m12 = _mm_loadu_si128((const __m128i *)(const void *)(words + t - 12));
        __m128i m8 = _mm_loadu_si128((const __m128i *)(const void *)(words + t - 8));
        __m128i m4 = _mm_loadu_si128((const __m128i *)(const void *)(words + t - 4));
        __m128i m14 = _mm_or_si128(_mm_srli_si128(m16, 8), _mm_slli_si128(m12, 8));
        __m128i value = _mm_xor_si128(_mm_xor_si128(m16, m14),
                                     _mm_xor_si128(m8, _mm_srli_si128(m4, 4)));
        value = xx_sha_rol1(value);
        /* The fourth word depends on the newly generated first word. */
        value = _mm_xor_si128(value, _mm_slli_si128(xx_sha_rol1(value), 12));
        _mm_storeu_si128((__m128i *)(void *)(words + t), value);
    }
}

XX_SHA_TARGET_SSE2
static void xx_sha256_schedule_sse2(const uint8_t *block, uint32_t words[64]) {
    unsigned t;
    for (t = 0; t < 16; t += 4) {
        __m128i value = _mm_loadu_si128((const __m128i *)(const void *)(block + 4U * t));
        _mm_storeu_si128((__m128i *)(void *)(words + t), xx_sha_big_endian(value));
    }
    for (t = 16; t < 64; t += 4) {
        __m128i m16 = _mm_loadu_si128((const __m128i *)(const void *)(words + t - 16));
        __m128i m12 = _mm_loadu_si128((const __m128i *)(const void *)(words + t - 12));
        __m128i m8 = _mm_loadu_si128((const __m128i *)(const void *)(words + t - 8));
        __m128i m4 = _mm_loadu_si128((const __m128i *)(const void *)(words + t - 4));
        __m128i m15 = _mm_or_si128(_mm_srli_si128(m16, 4), _mm_slli_si128(m12, 12));
        __m128i m7 = _mm_or_si128(_mm_srli_si128(m8, 4), _mm_slli_si128(m4, 12));
        __m128i value = _mm_add_epi32(_mm_add_epi32(m16, xx_sha256_sigma0(m15)), m7);
        value = _mm_add_epi32(value, xx_sha256_sigma1(_mm_srli_si128(m4, 8)));
        /* The high two words depend on the low two newly generated words. */
        value = _mm_add_epi32(value, xx_sha256_sigma1(_mm_slli_si128(value, 8)));
        _mm_storeu_si128((__m128i *)(void *)(words + t), value);
    }
}

static void xx_sha1_rounds(uint32_t state[5], const uint32_t words[80]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    unsigned t;
    for (t = 0; t < 80; ++t) {
        uint32_t f, k, next;
        if (t < 20) { f = d ^ (b & (c ^ d)); k = 0x5a827999U; }
        else if (t < 40) { f = b ^ c ^ d; k = 0x6ed9eba1U; }
        else if (t < 60) { f = (b & c) | (d & (b | c)); k = 0x8f1bbcdcU; }
        else { f = b ^ c ^ d; k = 0xca62c1d6U; }
        next = xx_sha_rotl(a, 5) + f + e + k + words[t];
        e = d; d = c; c = xx_sha_rotl(b, 30); b = a; a = next;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void xx_sha256_rounds(uint32_t state[8], const uint32_t words[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    unsigned t;
    for (t = 0; t < 64; ++t) {
        uint32_t s1 = xx_sha_rotr(e, 6) ^ xx_sha_rotr(e, 11) ^ xx_sha_rotr(e, 25);
        uint32_t ch = g ^ (e & (f ^ g));
        uint32_t temp1 = h + s1 + ch + xx_sha256_round_constants[t] + words[t];
        uint32_t s0 = xx_sha_rotr(a, 2) ^ xx_sha_rotr(a, 13) ^ xx_sha_rotr(a, 22);
        uint32_t maj = (a & b) | (c & (a | b));
        uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

XX_SHA_TARGET_SSE2
static XX_SHA_NOINLINE void xx_sha1_blocks_sse2(uint32_t state[5], const uint8_t *blocks, size_t count) {
    uint32_t words[80];
    while (count--) {
        xx_sha1_schedule_sse2(blocks, words);
        xx_sha1_rounds(state, words);
        blocks += 64;
    }
    xx_mem_zero(words, sizeof(words));
}

XX_SHA_TARGET_SSE2
static XX_SHA_NOINLINE void xx_sha256_blocks_sse2(uint32_t state[8], const uint8_t *blocks, size_t count) {
    uint32_t words[64];
    while (count--) {
        xx_sha256_schedule_sse2(blocks, words);
        xx_sha256_rounds(state, words);
        blocks += 64;
    }
    xx_mem_zero(words, sizeof(words));
}

#ifdef XX_SHA_HAVE_SHA_NI
XX_SHA_TARGET_SHA_NI
static XX_SHA_NOINLINE void xx_sha1_blocks_sha_ni(uint32_t state[5], const uint8_t *blocks, size_t count) {
    uint32_t words[80];
    while (count--) {
        __m128i abcd = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)(const void *)state), 0x1b);
        __m128i initial_abcd = abcd;
        __m128i previous_abcd = abcd;
        uint32_t initial_e = state[4];
        unsigned t;
        xx_sha1_schedule_sse2(blocks, words);
        for (t = 0; t < 80; t += 4) {
            __m128i before = abcd;
            __m128i message = _mm_shuffle_epi32(
                _mm_loadu_si128((const __m128i *)(const void *)(words + t)), 0x1b);
            if (t == 0) message = _mm_add_epi32(message, _mm_set_epi32((int)initial_e, 0, 0, 0));
            else message = _mm_sha1nexte_epu32(previous_abcd, message);
            if (t < 20) abcd = _mm_sha1rnds4_epu32(abcd, message, 0);
            else if (t < 40) abcd = _mm_sha1rnds4_epu32(abcd, message, 1);
            else if (t < 60) abcd = _mm_sha1rnds4_epu32(abcd, message, 2);
            else abcd = _mm_sha1rnds4_epu32(abcd, message, 3);
            previous_abcd = before;
        }
        abcd = _mm_add_epi32(abcd, initial_abcd);
        _mm_storeu_si128((__m128i *)(void *)state, _mm_shuffle_epi32(abcd, 0x1b));
        state[4] = initial_e + (uint32_t)_mm_cvtsi128_si32(_mm_shuffle_epi32(
            _mm_sha1nexte_epu32(previous_abcd, _mm_setzero_si128()), 0xff));
        blocks += 64;
    }
    xx_mem_zero(words, sizeof(words));
}

XX_SHA_TARGET_SHA_NI
static XX_SHA_NOINLINE void xx_sha256_blocks_sha_ni(uint32_t state[8], const uint8_t *blocks, size_t count) {
    __m128i message_words[16];
    uint32_t lanes[8];
    while (count--) {
        __m128i abef = _mm_set_epi32((int)state[0], (int)state[1], (int)state[4], (int)state[5]);
        __m128i cdgh = _mm_set_epi32((int)state[2], (int)state[3], (int)state[6], (int)state[7]);
        __m128i initial_abef = abef, initial_cdgh = cdgh;
        unsigned group;
        for (group = 0; group < 4; ++group) {
            message_words[group] = xx_sha_big_endian(_mm_loadu_si128(
                (const __m128i *)(const void *)(blocks + 16U * group)));
        }
        for (group = 4; group < 16; ++group) {
            __m128i minus7 = _mm_or_si128(_mm_srli_si128(message_words[group - 2], 4),
                                         _mm_slli_si128(message_words[group - 1], 12));
            __m128i value = _mm_sha256msg1_epu32(message_words[group - 4], message_words[group - 3]);
            value = _mm_add_epi32(value, minus7);
            message_words[group] = _mm_sha256msg2_epu32(value, message_words[group - 1]);
        }
        for (group = 0; group < 16; ++group) {
            __m128i message = _mm_add_epi32(message_words[group], _mm_loadu_si128(
                (const __m128i *)(const void *)(xx_sha256_round_constants + 4U * group)));
            cdgh = _mm_sha256rnds2_epu32(cdgh, abef, message);
            message = _mm_shuffle_epi32(message, 0x0e);
            abef = _mm_sha256rnds2_epu32(abef, cdgh, message);
        }
        abef = _mm_add_epi32(abef, initial_abef);
        cdgh = _mm_add_epi32(cdgh, initial_cdgh);
        _mm_storeu_si128((__m128i *)(void *)lanes, abef);
        _mm_storeu_si128((__m128i *)(void *)(lanes + 4), cdgh);
        state[0] = lanes[3]; state[1] = lanes[2]; state[2] = lanes[7]; state[3] = lanes[6];
        state[4] = lanes[1]; state[5] = lanes[0]; state[6] = lanes[5]; state[7] = lanes[4];
        blocks += 64;
    }
    xx_mem_zero(message_words, sizeof(message_words));
    xx_mem_zero(lanes, sizeof(lanes));
}
#endif /* XX_SHA_HAVE_SHA_NI */

static unsigned xx_sha_detect_capabilities(void) {
    unsigned result = 1U << XX_SHA_BACKEND_SCALAR;
    unsigned maximum = 0, edx = 0;
#if defined(XX_SHA_HAVE_SHA_NI) && XX_SHA_ENABLE_SHA_NI_BACKEND
    unsigned ebx7 = 0;
#endif
#if defined(_MSC_VER)
    int registers[4];
    __cpuid(registers, 0);
    maximum = (unsigned)registers[0];
    if (maximum >= 1) { __cpuidex(registers, 1, 0); edx = (unsigned)registers[3]; }
#if defined(XX_SHA_HAVE_SHA_NI) && XX_SHA_ENABLE_SHA_NI_BACKEND
    if (maximum >= 7) { __cpuidex(registers, 7, 0); ebx7 = (unsigned)registers[1]; }
#endif
#elif defined(__GNUC__) || defined(__clang__)
    unsigned eax, ebx, ecx;
    maximum = __get_cpuid_max(0, NULL);
    if (maximum >= 1) __cpuid_count(1, 0, eax, ebx, ecx, edx);
#if defined(XX_SHA_HAVE_SHA_NI) && XX_SHA_ENABLE_SHA_NI_BACKEND
    if (maximum >= 7) {
        unsigned leaf7_edx;
        __cpuid_count(7, 0, eax, ebx7, ecx, leaf7_edx);
    }
#endif
#endif
    if ((edx & (1U << 26)) != 0) {
        result |= 1U << XX_SHA_BACKEND_SSE2;
#if defined(XX_SHA_HAVE_SHA_NI) && XX_SHA_ENABLE_SHA_NI_BACKEND
        if ((ebx7 & (1U << 29)) != 0) result |= 1U << XX_SHA_BACKEND_SHA_NI;
#endif
    }
    return result;
}

#endif /* XX_SHA_X86 */

unsigned xx_sha_backend_capabilities(void) {
#ifdef XX_SHA_X86
#if defined(_MSC_VER)
    static volatile long cached = 0;
    long value = _InterlockedCompareExchange(&cached, 0, 0);
    if (value == 0) {
        value = (long)xx_sha_detect_capabilities();
        _InterlockedCompareExchange(&cached, value, 0);
    }
    return (unsigned)value;
#elif defined(__GNUC__) || defined(__clang__)
    static unsigned cached = 0;
    unsigned value = __atomic_load_n(&cached, __ATOMIC_RELAXED);
    if (value == 0) {
        value = xx_sha_detect_capabilities();
        __atomic_store_n(&cached, value, __ATOMIC_RELAXED);
    }
    return value;
#else
    return xx_sha_detect_capabilities();
#endif
#else
    return 1U << XX_SHA_BACKEND_SCALAR;
#endif
}

int xx_sha_selected_backend(void) {
    unsigned capabilities = xx_sha_backend_capabilities();
    if (capabilities & (1U << XX_SHA_BACKEND_SHA_NI)) return XX_SHA_BACKEND_SHA_NI;
    if (capabilities & (1U << XX_SHA_BACKEND_SSE2)) return XX_SHA_BACKEND_SSE2;
    return XX_SHA_BACKEND_SCALAR;
}

const char *xx_sha_backend_name(int backend) {
    switch (backend) {
    case XX_SHA_BACKEND_AUTO: return "auto";
    case XX_SHA_BACKEND_SCALAR: return "scalar";
    case XX_SHA_BACKEND_SSE2: return "sse2";
    case XX_SHA_BACKEND_SHA_NI: return "sha-ni";
    default: return "unsupported";
    }
}

bool xx_sha1_blocks_backend(uint32_t state[5], const uint8_t *blocks,
                             size_t block_count, int backend) {
    if (!state || (!blocks && block_count != 0) || block_count > SIZE_MAX / 64U) return false;
    if (backend == XX_SHA_BACKEND_AUTO) backend = xx_sha_selected_backend();
    if (backend < XX_SHA_BACKEND_SCALAR || backend > XX_SHA_BACKEND_SHA_NI ||
        !(xx_sha_backend_capabilities() & (1U << backend))) return false;
    if (block_count == 0) return true;
#ifdef XX_SHA_X86
#ifdef XX_SHA_HAVE_SHA_NI
    if (backend == XX_SHA_BACKEND_SHA_NI) { xx_sha1_blocks_sha_ni(state, blocks, block_count); return true; }
#endif
    if (backend == XX_SHA_BACKEND_SSE2) { xx_sha1_blocks_sse2(state, blocks, block_count); return true; }
#endif
    xx_sha1_blocks_scalar(state, blocks, block_count);
    return true;
}

bool xx_sha256_blocks_backend(uint32_t state[8], const uint8_t *blocks,
                               size_t block_count, int backend) {
    if (!state || (!blocks && block_count != 0) || block_count > SIZE_MAX / 64U) return false;
    if (backend == XX_SHA_BACKEND_AUTO) backend = xx_sha_selected_backend();
    if (backend < XX_SHA_BACKEND_SCALAR || backend > XX_SHA_BACKEND_SHA_NI ||
        !(xx_sha_backend_capabilities() & (1U << backend))) return false;
    if (block_count == 0) return true;
#ifdef XX_SHA_X86
#ifdef XX_SHA_HAVE_SHA_NI
    if (backend == XX_SHA_BACKEND_SHA_NI) { xx_sha256_blocks_sha_ni(state, blocks, block_count); return true; }
#endif
    if (backend == XX_SHA_BACKEND_SSE2) { xx_sha256_blocks_sse2(state, blocks, block_count); return true; }
#endif
    xx_sha256_blocks_scalar(state, blocks, block_count);
    return true;
}

void xx_sha1_blocks(uint32_t state[5], const uint8_t *blocks, size_t block_count) {
    (void)xx_sha1_blocks_backend(state, blocks, block_count, XX_SHA_BACKEND_AUTO);
}

void xx_sha256_blocks(uint32_t state[8], const uint8_t *blocks, size_t block_count) {
    (void)xx_sha256_blocks_backend(state, blocks, block_count, XX_SHA_BACKEND_AUTO);
}
