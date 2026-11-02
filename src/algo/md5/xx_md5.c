/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/algo/md5/xx_md5.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "../../global/xx_read_buffer_internal.h"

static void md5_clear(void *data, size_t size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)data;
    while (size-- > 0U) *bytes++ = 0U;
}

static uint32_t md5_rotl32(uint32_t value, unsigned bits)
{
    return (uint32_t)((value << bits) | (value >> (32U - bits)));
}

/* ------------------------------------------------------------------- MD5 -- */

static const uint32_t MD5_K[64] = {0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U, 0x698098d8U, 0x8b44f7afU,
                                   0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
                                   0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U, 0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U,
                                   0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
                                   0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U, 0xf4292244U, 0x432aff97U,
                                   0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U, 0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
                                   0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U};

static const unsigned char MD5_S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static void md5_transform(uint32_t *state, const uint8_t *block)
{
    uint32_t m[16];
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    unsigned i;

    for (i = 0; i < 16U; ++i) {
        m[i] = (uint32_t)block[i * 4U] | ((uint32_t)block[i * 4U + 1U] << 8) | ((uint32_t)block[i * 4U + 2U] << 16) | ((uint32_t)block[i * 4U + 3U] << 24);
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
        b += md5_rotl32(f, MD5_S[i]);
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_blocks(xx_md5_context *context, const uint8_t *blocks, size_t count)
{
    while (count-- > 0U) {
        md5_transform(context->state, blocks);
        blocks += XX_MD5_BLOCK_SIZE;
    }
}

bool xx_md5_init(xx_md5_context *context)
{
    if (!context) return false;
    xx_rt_memset(context, 0, sizeof(*context));
    context->state[0] = 0x67452301U;
    context->state[1] = 0xefcdab89U;
    context->state[2] = 0x98badcfeU;
    context->state[3] = 0x10325476U;
    context->initialized = true;
    return true;
}

void xx_md5_update(xx_md5_context *ctx, const void *data, size_t size)
{
    const uint8_t *input = (const uint8_t *)data;
    size_t taken = 0U;

    if (!ctx || !ctx->initialized || (!data && size != 0U)) return;
    if (size == 0U) return;

    ctx->length += (uint64_t)size;

    /* Top up a partial block first, so the fast path below always sees a
     * block-aligned start. */
    if (ctx->buffered != 0U) {
        size_t need = XX_MD5_BLOCK_SIZE - ctx->buffered;
        size_t copy = (size < need) ? size : need;
        xx_rt_memcpy(ctx->buffer + ctx->buffered, input, copy);
        ctx->buffered += copy;
        taken = copy;
        if (ctx->buffered < XX_MD5_BLOCK_SIZE) return;
        md5_transform(ctx->state, ctx->buffer);
        ctx->buffered = 0U;
    }

    if (size - taken >= XX_MD5_BLOCK_SIZE) {
        size_t blocks = (size - taken) / XX_MD5_BLOCK_SIZE;
        md5_blocks(ctx, input + taken, blocks);
        taken += blocks * XX_MD5_BLOCK_SIZE;
    }

    if (taken < size) {
        ctx->buffered = size - taken;
        xx_rt_memcpy(ctx->buffer, input + taken, ctx->buffered);
    }
}

bool xx_md5_final(xx_md5_context *context, void *out, size_t out_size)
{
    uint8_t *digest = (uint8_t *)out;
    uint64_t bits;
    size_t i;
    if (!context || !context->initialized || !out || out_size < XX_MD5_DIGEST_SIZE) return false;
    bits = context->length * 8U;
    context->buffer[context->buffered++] = 0x80U;
    if (context->buffered > XX_MD5_BLOCK_SIZE - 8U) {
        xx_rt_memset(context->buffer + context->buffered, 0, XX_MD5_BLOCK_SIZE - context->buffered);
        md5_transform(context->state, context->buffer);
        context->buffered = 0U;
    }
    xx_rt_memset(context->buffer + context->buffered, 0, XX_MD5_BLOCK_SIZE - 8U - context->buffered);
    for (i = 0; i < 8U; ++i) context->buffer[XX_MD5_BLOCK_SIZE - 8U + i] = (uint8_t)(bits >> (8U * i));
    md5_transform(context->state, context->buffer);
    for (i = 0; i < 4U; ++i) {
        uint32_t value = context->state[i];
        digest[i * 4U] = (uint8_t)value;
        digest[i * 4U + 1U] = (uint8_t)(value >> 8);
        digest[i * 4U + 2U] = (uint8_t)(value >> 16);
        digest[i * 4U + 3U] = (uint8_t)(value >> 24);
    }
    md5_clear(context, sizeof(*context));
    return true;
}

bool xx_md5_memory(const void *data, size_t size, void *digest)
{
    xx_md5_context context;
    if ((!data && size) || !digest) return false;
    xx_md5_init(&context);
    xx_md5_update(&context, data, size);
    return xx_md5_final(&context, digest, XX_MD5_DIGEST_SIZE);
}

bool xx_md5_device(xx_io_device *device, int64_t offset, int64_t size, void *digest, size_t digest_size, xx_pd_struct *pd)
{
    xx_md5_context context;
    uint8_t *chunk = NULL;
    int64_t total, done = 0;
    size_t capacity;
    bool ok = false;
    if (!device || !digest || digest_size < XX_MD5_DIGEST_SIZE || offset < 0 || xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(device);
    if (total < 0 || offset > total) return false;
    if (size < 0) size = total - offset;
    if (size > total - offset) return false;
    xx_md5_init(&context);
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) goto finish;
    if (size > 0) {
        chunk = (uint8_t *)xx_read_buffer_alloc((uint64_t)size, &capacity);
        if (!chunk) goto finish;
        while (done < size) {
            size_t request = (uint64_t)(size - done) < capacity ? (size_t)(size - done) : capacity;
            ssize_t got;
            if (xx_pd_is_stopped(pd)) goto finish;
            got = xx_io_read(device, chunk, request);
            if (got <= 0 || (size_t)got > request || xx_pd_is_stopped(pd)) goto finish;
            xx_md5_update(&context, chunk, (size_t)got);
            done += (int64_t)got;
        }
    }
    if (!xx_pd_is_stopped(pd)) ok = xx_md5_final(&context, digest, digest_size);
finish:
    xx_mem_free(chunk);
    md5_clear(&context, sizeof(context));
    return ok;
}

char *xx_md5_digest_hex(const void *digest)
{
    static const char digits[] = "0123456789ABCDEF";
    const uint8_t *bytes = (const uint8_t *)digest;
    char *result;
    size_t i;
    if (!digest) return NULL;
    result = (char *)xx_mem_alloc(XX_MD5_DIGEST_SIZE * 2U + 1U);
    if (!result) return NULL;
    for (i = 0; i < XX_MD5_DIGEST_SIZE; ++i) {
        result[i * 2U] = digits[bytes[i] >> 4];
        result[i * 2U + 1U] = digits[bytes[i] & 0x0f];
    }
    result[XX_MD5_DIGEST_SIZE * 2U] = '\0';
    return result;
}

char *xx_md5_hex(const void *data, size_t size)
{
    uint8_t digest[XX_MD5_DIGEST_SIZE];
    char *result;
    if (!xx_md5_memory(data, size, digest)) xx_rt_memset(digest, 0, sizeof(digest));
    result = xx_md5_digest_hex(digest);
    md5_clear(digest, sizeof(digest));
    return result;
}

char *xx_md5(xx_io_device *device, int64_t offset, int64_t size)
{
    xx_io_device *temporary = NULL;
    xx_md5_context context;
    uint8_t digest[XX_MD5_DIGEST_SIZE] = {0};
    const unsigned char *whole;
    int64_t done = 0;
    size_t capacity;
    char *result;
    if (!xx_io_range_clamp(device, offset, &size) || !size) return xx_md5_hex(NULL, 0);
    capacity = xx_io_buffer_capacity(device);
    if (!capacity) {
        capacity = xx_get_file_buffer_size();
        if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
        if (capacity > (size_t)PTRDIFF_MAX) capacity = (size_t)PTRDIFF_MAX;
        temporary = xx_io_buffer_open(device, capacity, 0, false);
        if (!temporary) return xx_md5_digest_hex(digest);
        device = temporary;
    }
    whole = xx_io_buffer_whole(device, (uint64_t)size);
    if (whole && (uint64_t)size <= SIZE_MAX) {
        result = xx_md5_hex(whole + offset, (size_t)size);
        if (temporary) xx_io_close(temporary);
        return result;
    }
    xx_md5_init(&context);
    while (done < size) {
        size_t count = (uint64_t)(size - done) < capacity ? (size_t)(size - done) : capacity;
        const unsigned char *view = xx_io_buffer_view(device, offset + done, &count);
        if (!view || !count) goto finish;
        xx_md5_update(&context, view, count);
        done += (int64_t)count;
    }
    xx_md5_final(&context, digest, sizeof(digest));
finish:
    md5_clear(&context, sizeof(context));
    result = xx_md5_digest_hex(digest);
    md5_clear(digest, sizeof(digest));
    if (temporary) xx_io_close(temporary);
    return result;
}
