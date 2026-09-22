/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Tivoli Filepack Block (.PKT) container codec, ported from
 * XArchive/Algos/xtivolidecoder.cpp: the "compress=native" LZ77, the block
 * chain that carries it, and the running-MD5 tail that validates it.
 *
 * compress=native
 * ---------------
 * LZ77 over a 4096-byte ring PRESET TO 0x00 (not 0x20), with 16-bit LITTLE
 * endian flag words consumed LSB first, 16 items per word:
 *
 *   flag 0 -> one literal byte
 *   flag 1 -> two bytes b0,b1:
 *               length   = (b0 & 0x0f) + 1        (1..16)
 *               distance = ((b0 >> 4) << 8) | b1  (12 bits)
 *               source   = (position - distance) & 0xfff
 *
 * THE TRAP IS THAT LAST LINE: there is NO -1 bias on the distance.  Nearly
 * every other 12-bit LZ77 computes (position - distance - 1); writing that
 * here is wrong by one byte per match and only diverges once the first match
 * lands.  A distance of 0 is legal and means "the byte about to be written",
 * which is how the format spells a run -- and it reads the ring cell that is
 * about to be overwritten, i.e. the byte 4096 positions back (0x00 while the
 * ring is still untouched).  DELIBERATE: do not "fix" it.
 *
 * The codec is driven by a COMPRESSED byte budget, not by an output length,
 * and the budget is charged for the flag words as well as the items.  Because
 * a budget can run out in the middle of an item, the reader legitimately
 * overshoots it by up to three bytes; the chain walker must resume from the
 * offset actually reached, never from offset + budget.  DELIBERATE.
 *
 * Block chain
 * -----------
 *   2 bytes BIG endian, h:
 *     h == 0              end of chain
 *     (h & 0x8000) == 0   compressed block, h = compressed byte budget
 *     (h & 0x8000) != 0   stored block, length = h & 0x7fff
 *
 * The last 8 bytes of each block are not payload: they are the first 8 bytes
 * of the MD5 digest of every payload emitted SO FAR -- one running MD5 context
 * over the whole stream, snapshotted and finalised per block, not a per-block
 * digest.  The digest bytes are never fed back into the hash and the context is
 * never restarted; either mistake matches on block one and fails on block two.
 *
 * MD5 is carried locally (same constants and the same snapshot-a-copy finalise
 * as the reference) because the library has no shared MD5 and the format needs
 * a snapshot of a still-running context.  The tables are const, so there is no
 * mutable module state.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/tivoli/xx_tivoli.h"

#define TIV_RING_SIZE 0x1000U
#define TIV_RING_MASK 0x0fffU
#define TIV_TAIL_SIZE 8U
#define TIV_MAX_BLOCK 0x4000U
#define TIV_MAX_BLOCKS 0x100000U
#define TIV_HEADER_SIZE 0x4fU

/* --------------------------------------------------------------------- */
/* MD5                                                                    */
/* --------------------------------------------------------------------- */
static const uint32_t g_tiv_md5_k[64] = {
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
    0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U
};

static const uint8_t g_tiv_md5_s[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

typedef struct tiv_md5 {
    uint32_t state[4];
    uint64_t length;
    uint8_t buffer[64];
} tiv_md5;

static void tiv_md5_transform(uint32_t *state, const uint8_t *block) {
    uint32_t words[16];
    uint32_t a, b, c, d;
    unsigned i;

    for (i = 0U; i < 16U; ++i) {
        words[i] = (uint32_t)block[i * 4U] |
                   ((uint32_t)block[i * 4U + 1U] << 8) |
                   ((uint32_t)block[i * 4U + 2U] << 16) |
                   ((uint32_t)block[i * 4U + 3U] << 24);
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];

    for (i = 0U; i < 64U; ++i) {
        uint32_t f;
        unsigned index;
        unsigned shift;
        if (i < 16U) {
            f = (b & c) | ((~b) & d);
            index = i;
        } else if (i < 32U) {
            f = (d & b) | ((~d) & c);
            index = (5U * i + 1U) & 15U;
        } else if (i < 48U) {
            f = b ^ c ^ d;
            index = (3U * i + 5U) & 15U;
        } else {
            f = c ^ (b | (~d));
            index = (7U * i) & 15U;
        }
        f = (uint32_t)(f + a + g_tiv_md5_k[i] + words[index]);
        a = d;
        d = c;
        c = b;
        shift = g_tiv_md5_s[i];
        b = (uint32_t)(b + ((f << shift) | (f >> (32U - shift))));
    }

    state[0] = (uint32_t)(state[0] + a);
    state[1] = (uint32_t)(state[1] + b);
    state[2] = (uint32_t)(state[2] + c);
    state[3] = (uint32_t)(state[3] + d);
}

static void tiv_md5_init(tiv_md5 *ctx) {
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xefcdab89U;
    ctx->state[2] = 0x98badcfeU;
    ctx->state[3] = 0x10325476U;
    ctx->length = 0U;
    xx_rt_memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

static void tiv_md5_update(tiv_md5 *ctx, const uint8_t *data, size_t size) {
    unsigned fill = (unsigned)(ctx->length & 63U);
    size_t i;
    for (i = 0U; i < size; ++i) {
        ctx->buffer[fill] = data[i];
        ++fill;
        if (fill == 64U) {
            tiv_md5_transform(ctx->state, ctx->buffer);
            fill = 0U;
        }
    }
    ctx->length += (uint64_t)size;
}

/* Snapshot: the caller's context keeps running, exactly like hashlib's
 * md5.digest() on a context that is still being fed. */
static void tiv_md5_digest(const tiv_md5 *ctx, uint8_t *digest) {
    tiv_md5 copy = *ctx;
    uint64_t bits = copy.length * 8U;
    unsigned fill = (unsigned)(copy.length & 63U);
    unsigned i;

    copy.buffer[fill] = 0x80U;
    ++fill;
    if (fill > 56U) {
        while (fill < 64U) {
            copy.buffer[fill] = 0U;
            ++fill;
        }
        tiv_md5_transform(copy.state, copy.buffer);
        fill = 0U;
    }
    while (fill < 56U) {
        copy.buffer[fill] = 0U;
        ++fill;
    }
    for (i = 0U; i < 8U; ++i) {
        copy.buffer[56U + i] = (uint8_t)((bits >> (8U * i)) & 0xffU);
    }
    tiv_md5_transform(copy.state, copy.buffer);

    for (i = 0U; i < 4U; ++i) {
        digest[i * 4U + 0U] = (uint8_t)(copy.state[i] & 0xffU);
        digest[i * 4U + 1U] = (uint8_t)((copy.state[i] >> 8) & 0xffU);
        digest[i * 4U + 2U] = (uint8_t)((copy.state[i] >> 16) & 0xffU);
        digest[i * 4U + 3U] = (uint8_t)((copy.state[i] >> 24) & 0xffU);
    }
}

/* --------------------------------------------------------------------- */
/* compress=native                                                        */
/* --------------------------------------------------------------------- */
typedef struct tiv_state {
    uint8_t ring[TIV_RING_SIZE];
    uint8_t block[TIV_MAX_BLOCK];
    size_t block_size;
} tiv_state;

/* Decode one block body.  `budget` is the COMPRESSED byte allowance; the
 * decoded bytes land in state->block / state->block_size, capped at
 * TIV_MAX_BLOCK.  *next receives the input offset actually reached, which may
 * be up to three bytes past offset + budget. */
static bool tiv_decode_native(tiv_state *state, const uint8_t *input,
                              size_t input_size, size_t offset, size_t budget,
                              size_t *next) {
    size_t position = offset;
    int64_t remaining = (int64_t)budget;
    uint32_t cursor = 0U;
    uint32_t flags = 0U;
    unsigned flag_count = 0U;

    if (offset > input_size) return false;

    /* Preset to 0x00.  A distance may reach into the untouched part of the
     * ring on purpose, so the fill value is part of the format. */
    xx_rt_memset(state->ring, 0, sizeof(state->ring));
    state->block_size = 0U;

    while (remaining > 0) {
        uint32_t bit;

        if (flag_count == 0U) {
            if ((position + 2U) > input_size) return false;
            flags = (uint32_t)input[position] |
                    ((uint32_t)input[position + 1U] << 8);
            position += 2U;
            remaining -= 2;
            flag_count = 16U;
        }

        bit = flags & 1U;
        flags >>= 1;
        --flag_count;

        if (bit == 0U) {
            uint8_t byte;
            if (position >= input_size) return false;
            byte = input[position];
            ++position;
            --remaining;
            if (state->block_size >= TIV_MAX_BLOCK) return false;
            state->ring[cursor] = byte;
            state->block[state->block_size++] = byte;
            cursor = (cursor + 1U) & TIV_RING_MASK;
        } else {
            uint32_t b0, b1, distance, source;
            unsigned length, i;
            if ((position + 2U) > input_size) return false;
            b0 = input[position];
            b1 = input[position + 1U];
            position += 2U;
            remaining -= 2;

            length = (unsigned)(b0 & 0x0fU) + 1U;
            distance = ((b0 >> 4) << 8) | b1;
            /* No -1 bias: the source is (position - distance). */
            source = (cursor - distance) & TIV_RING_MASK;

            for (i = 0U; i < length; ++i) {
                uint8_t byte;
                if (state->block_size >= TIV_MAX_BLOCK) return false;
                byte = state->ring[source];
                state->ring[cursor] = byte;
                state->block[state->block_size++] = byte;
                source = (source + 1U) & TIV_RING_MASK;
                cursor = (cursor + 1U) & TIV_RING_MASK;
            }
        }
    }

    if (next) *next = position;
    return true;
}

/* --------------------------------------------------------------------- */
/* Header + block chain                                                   */
/* --------------------------------------------------------------------- */
/* The reference detector does not compare the whole 79-byte header line, only
 * six anchors in it; a producer is free to vary the fpname field between
 * them.  Kept as-is on purpose. */
static bool tiv_check_header(const uint8_t *input, size_t input_size) {
    if (input_size < TIV_HEADER_SIZE) return false;
    if (xx_rt_memcmp(input + 0x00, "    ", 4) != 0) return false;
    if (xx_rt_memcmp(input + 0x04, "79 T", 4) != 0) return false;
    if (xx_rt_memcmp(input + 0x08, "FPB-", 4) != 0) return false;
    if (xx_rt_memcmp(input + 0x3c, "d5 c", 4) != 0) return false;
    if (xx_rt_memcmp(input + 0x4c, "ve", 2) != 0) return false;
    if (input[0x4e] != 0x0aU) return false;
    return true;
}

static bool tiv_run(const uint8_t *input, size_t input_size, uint8_t *output,
                    size_t limit, size_t *produced, size_t *consumed) {
    tiv_state *state;
    tiv_md5 ctx;
    size_t position;
    size_t out_at = 0U;
    uint32_t blocks = 0U;
    bool result = false;

    if (!input) return false;
    if (!tiv_check_header(input, input_size)) return false;

    /* 20 KB of ring plus block buffer is more than belongs on a decoder's
     * stack, so it comes from the library allocator. */
    state = (tiv_state *)xx_mem_alloc(sizeof(tiv_state));
    if (!state) return false;

    tiv_md5_init(&ctx);
    position = TIV_HEADER_SIZE;

    for (;;) {
        uint32_t header;
        size_t body;
        uint8_t digest[16];

        if (position >= input_size) {
            result = true;
            break;
        }
        /* A trailing odd byte is a clean end of chain, not a failure. */
        if ((position + 2U) > input_size) {
            result = true;
            break;
        }

        header = ((uint32_t)input[position] << 8) | (uint32_t)input[position + 1U];
        position += 2U;

        if ((header & 0x8000U) == 0U) {
            size_t next = 0U;
            if (header == 0U) {
                /* End-of-chain marker.  `consumed` reports the offset just
                 * past it. */
                result = true;
                break;
            }
            if (!tiv_decode_native(state, input, input_size, position,
                                   (size_t)header, &next)) {
                break;
            }
            if (state->block_size < TIV_TAIL_SIZE) break;
            position = next;
        } else {
            size_t length = (size_t)(header & 0x7fffU);
            /* A stored block must have at least one payload byte on top of
             * the 8 digest bytes. */
            if ((length < (TIV_TAIL_SIZE + 1U)) || (length > TIV_MAX_BLOCK)) {
                break;
            }
            if ((position + length) > input_size) break;
            xx_rt_memcpy(state->block, input + position, length);
            state->block_size = length;
            position += length;
        }

        body = state->block_size - TIV_TAIL_SIZE;
        tiv_md5_update(&ctx, state->block, body);
        tiv_md5_digest(&ctx, digest);
        if (xx_rt_memcmp(digest, state->block + body, TIV_TAIL_SIZE) != 0) {
            break;
        }

        if (body > (limit - out_at)) break; /* out of output capacity */
        if (output && body) xx_rt_memcpy(output + out_at, state->block, body);
        out_at += body;

        ++blocks;
        if (blocks > TIV_MAX_BLOCKS) break;
    }

    xx_mem_free(state);

    if (!result) return false;
    if (produced) *produced = out_at;
    if (consumed) *consumed = position;
    return true;
}

bool xx_tivoli_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written) {
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!output) return false;
    if (!tiv_run(input, input_size, output, output_size, &produced, NULL)) {
        return false;
    }
    if (written) *written = produced;
    /* Short of the declared size means the caller was told the wrong length;
     * overrunning it is caught inside tiv_run(). */
    return produced == output_size;
}

bool xx_tivoli_scan_memory(const uint8_t *input, size_t input_size,
                           size_t max_output, size_t *consumed,
                           size_t *produced) {
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (max_output == 0U) return false;
    return tiv_run(input, input_size, NULL, max_output, produced, consumed);
}
