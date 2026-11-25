/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/algo/ripemd256/xx_ripemd256.h"
#include "xxfclib/rt/xx_rt.h"

/* Algorithm specification: Bosselaers, ripemd/rmd256.txt. Byte loads and
 * stores are explicit so unaligned input and either host endianness work. */
static const uint8_t rmd_words[2][64] = {{0, 1,  2,  3, 4, 5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 7, 4, 13, 1,  10, 6, 15, 3, 12, 0, 9, 5,  2,  14, 11, 8,
                                          3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0,  6,  13, 11, 5,  12, 1, 9, 11, 10, 0,  8, 12, 4, 13, 3, 7, 15, 14, 5,  6,  2},
                                         {5,  14, 7, 0, 9, 2,  11, 4, 13, 6, 15, 8, 1,  10, 3, 12, 6, 11, 3, 7, 0, 13, 5,  10, 14, 15, 8, 12, 4, 9, 1,  2,
                                          15, 5,  1, 3, 7, 14, 6,  9, 11, 8, 12, 2, 10, 0,  4, 13, 8, 6,  4, 1, 3, 11, 15, 0,  5,  12, 2, 13, 9, 7, 10, 14}};
static const uint8_t rmd_shifts[2][64] = {{11, 14, 15, 12, 5,  8, 7,  9,  11, 13, 14, 15, 6, 7,  9, 8, 7,  6,  8,  13, 11, 9,  7, 15, 7, 12, 15, 9, 11, 7, 13, 12,
                                           11, 13, 6,  7,  14, 9, 13, 15, 14, 8,  13, 6,  5, 12, 7, 5, 11, 12, 14, 15, 14, 15, 9, 8,  9, 14, 5,  6, 8,  6, 5,  12},
                                          {8, 9, 9,  11, 13, 15, 15, 5,  7,  7,  8, 11, 14, 14, 12, 6, 9,  13, 15, 7,  12, 8,  9, 11, 7, 7, 12, 7, 6,  15, 13, 11,
                                           9, 7, 15, 11, 8,  6,  6,  14, 12, 13, 5, 14, 13, 13, 7,  5, 15, 5,  8,  11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5,  15, 8}};
static const uint32_t rmd_constants[2][4] = {{0U, 0x5a827999U, 0x6ed9eba1U, 0x8f1bbcdcU}, {0x50a28be6U, 0x5c4dd124U, 0x6d703ef3U, 0U}};

static void rmd_clear(void *data, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size-- > 0U) *p++ = 0U;
}

static uint32_t rmd_function(unsigned round, uint32_t b, uint32_t c, uint32_t d)
{
    switch (round) {
        case 0: return b ^ c ^ d;
        case 1: return (b & c) | (~b & d);
        case 2: return (b | ~c) ^ d;
        default: return (b & d) | (c & ~d);
    }
}

static void rmd_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t words[16], work[8];
    unsigned i, lane;
    for (i = 0; i < 16U; ++i) {
        words[i] = (uint32_t)block[4U * i] | ((uint32_t)block[4U * i + 1U] << 8) | ((uint32_t)block[4U * i + 2U] << 16) | ((uint32_t)block[4U * i + 3U] << 24);
    }
    for (i = 0; i < 8U; ++i) work[i] = state[i];
    for (i = 0; i < 64U; ++i) {
        unsigned round = i / 16U;
        for (lane = 0; lane < 2U; ++lane) {
            uint32_t *v = work + lane * 4U;
            uint32_t t = v[0] + rmd_function(lane ? 3U - round : round, v[1], v[2], v[3]) + words[rmd_words[lane][i]] + rmd_constants[lane][round];
            unsigned shift = rmd_shifts[lane][i];
            t = (t << shift) | (t >> (32U - shift));
            v[0] = v[3];
            v[3] = v[2];
            v[2] = v[1];
            v[1] = t;
        }
        if ((i & 15U) == 15U) {
            uint32_t t = work[round];
            work[round] = work[round + 4U];
            work[round + 4U] = t;
        }
    }
    for (i = 0; i < 8U; ++i) state[i] += work[i];
    rmd_clear(words, sizeof(words));
    rmd_clear(work, sizeof(work));
}

bool xx_ripemd256_init(xx_ripemd256_context *context)
{
    static const uint32_t initial[8] = {0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U, 0x76543210U, 0xfedcba98U, 0x89abcdefU, 0x01234567U};
    unsigned i;
    if (!context) return false;
    xx_rt_memset(context, 0, sizeof(*context));
    for (i = 0; i < 8U; ++i) context->state[i] = initial[i];
    context->initialized = true;
    return true;
}

bool xx_ripemd256_update(xx_ripemd256_context *context, const void *data, size_t size)
{
    const uint8_t *input = (const uint8_t *)data;
    if (!context || !context->initialized || (!input && size) || context->buffered >= 64U) return false;
    context->length += (uint64_t)size;
    while (size) {
        if (!context->buffered && size >= 64U) {
            rmd_transform(context->state, input);
            input += 64U;
            size -= 64U;
        } else {
            size_t take = 64U - context->buffered;
            if (take > size) take = size;
            xx_rt_memcpy(context->buffer + context->buffered, input, take);
            context->buffered += take;
            input += take;
            size -= take;
            if (context->buffered == 64U) {
                rmd_transform(context->state, context->buffer);
                context->buffered = 0U;
            }
        }
    }
    return true;
}

bool xx_ripemd256_final(xx_ripemd256_context *context, void *out, size_t digest_size)
{
    uint8_t *digest = (uint8_t *)out;
    uint64_t bits;
    unsigned i;
    if (!context || !context->initialized || !out || digest_size < 32U || context->buffered >= 64U) return false;
    bits = context->length * 8U;
    context->buffer[context->buffered++] = 0x80U;
    if (context->buffered > 56U) {
        xx_rt_memset(context->buffer + context->buffered, 0, 64U - context->buffered);
        rmd_transform(context->state, context->buffer);
        context->buffered = 0U;
    }
    xx_rt_memset(context->buffer + context->buffered, 0, 56U - context->buffered);
    for (i = 0; i < 8U; ++i) context->buffer[56U + i] = (uint8_t)(bits >> (8U * i));
    rmd_transform(context->state, context->buffer);
    for (i = 0; i < 32U; ++i) digest[i] = (uint8_t)(context->state[i / 4U] >> (8U * (i % 4U)));
    rmd_clear(context, sizeof(*context));
    return true;
}

bool xx_ripemd256_memory(const void *input, size_t size, void *digest)
{
    xx_ripemd256_context context;
    if ((!input && size) || !digest) return false;
    xx_ripemd256_init(&context);
    xx_ripemd256_update(&context, input, size);
    return xx_ripemd256_final(&context, digest, 32U);
}
