/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original scalar BLAKE2sp implementation of the algorithm specified at
 * https://www.blake2.net/blake2.pdf (section 2.11) and RFC 7693.
 * The eight independent leaves are evaluated sequentially, without threads.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/blake2/xx_blake2.h"
#include <string.h>

#define XX_BLAKE2SP_READY UINT32_C(0x32535042)

static const uint32_t xx_blake2_initial[8] = {
    UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
    UINT32_C(0x510e527f), UINT32_C(0x9b05688c), UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)
};

/* Message-word permutation, a fixed part of the BLAKE2 specification. */
static const uint8_t xx_blake2_permutation[10][16] = {
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
    {11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},
    {7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
    {9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},
    {2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
    {12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},
    {13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
    {6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},
    {10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0}
};

static void xx_blake2_wipe(void *data, size_t size) {
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size) { *p++ = 0; --size; }
}

static uint32_t xx_blake2_rotate(uint32_t word, unsigned bits) {
    return (word >> bits) | (word << (32U - bits));
}

static void xx_blake2_mix(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d,
                          uint32_t first, uint32_t second) {
    *a += *b + first;
    *d = xx_blake2_rotate(*d ^ *a, 16);
    *c += *d;
    *b = xx_blake2_rotate(*b ^ *c, 12);
    *a += *b + second;
    *d = xx_blake2_rotate(*d ^ *a, 8);
    *c += *d;
    *b = xx_blake2_rotate(*b ^ *c, 7);
}

static void xx_blake2_compress(xx_blake2sp_node *node, bool final, bool last_node) {
    uint32_t message[16], work[16];
    unsigned i, round;
    for (i = 0; i < 16; ++i) {
        const uint8_t *p = node->pending + 4U * i;
        message[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    for (i = 0; i < 8; ++i) {
        work[i] = node->words[i];
        work[i + 8U] = xx_blake2_initial[i];
    }
    work[12] ^= (uint32_t)node->compressed_size;
    work[13] ^= (uint32_t)(node->compressed_size >> 32);
    if (final) work[14] = ~work[14];
    if (final && last_node) work[15] = ~work[15];
    for (round = 0; round < 10; ++round) {
        const uint8_t *order = xx_blake2_permutation[round];
        for (i = 0; i < 4; ++i) {
            xx_blake2_mix(work + i, work + i + 4U, work + i + 8U,
                          work + i + 12U, message[order[2U * i]],
                          message[order[2U * i + 1U]]);
        }
        for (i = 0; i < 4; ++i) {
            xx_blake2_mix(work + i, work + 4U + ((i + 1U) & 3U),
                          work + 8U + ((i + 2U) & 3U),
                          work + 12U + ((i + 3U) & 3U),
                          message[order[8U + 2U * i]],
                          message[order[9U + 2U * i]]);
        }
    }
    for (i = 0; i < 8; ++i) node->words[i] ^= work[i] ^ work[i + 8U];
    xx_blake2_wipe(message, sizeof(message));
    xx_blake2_wipe(work, sizeof(work));
}

static void xx_blake2_node_init(xx_blake2sp_node *node, unsigned index, bool root) {
    xx_rt_memset(node, 0, sizeof(*node));
    xx_rt_memcpy(node->words, xx_blake2_initial, sizeof(node->words));
    /* Fixed digest=32, key=0, fanout=8, depth=2, leaf length=0,
     * node offset=index, node depth=0/1, inner digest length=32. */
    node->words[0] ^= UINT32_C(0x02080020);
    node->words[2] ^= (uint32_t)index;
    node->words[3] ^= UINT32_C(0x20000000) | (root ? UINT32_C(0x00010000) : 0U);
}

static void xx_blake2_node_update(xx_blake2sp_node *node,
                                 const uint8_t *data, size_t size) {
    while (size) {
        size_t take;
        /* Retain a full last block until another byte proves it non-final. */
        if (node->pending_size == sizeof(node->pending)) {
            node->compressed_size += sizeof(node->pending);
            xx_blake2_compress(node, false, false);
            node->pending_size = 0;
        }
        take = sizeof(node->pending) - node->pending_size;
        if (take > size) take = size;
        xx_rt_memcpy(node->pending + node->pending_size, data, take);
        node->pending_size += take;
        data += take;
        size -= take;
    }
}

static void xx_blake2_node_final(xx_blake2sp_node *node, bool last_node,
                                uint8_t digest[32]) {
    unsigned i;
    node->compressed_size += node->pending_size;
    xx_rt_memset(node->pending + node->pending_size, 0,
           sizeof(node->pending) - node->pending_size);
    xx_blake2_compress(node, true, last_node);
    for (i = 0; i < 32; ++i)
        digest[i] = (uint8_t)(node->words[i / 4U] >> (8U * (i & 3U)));
}

bool xx_blake2sp_init(xx_blake2sp_context *context) {
    unsigned i;
    if (!context) return false;
    xx_rt_memset(context, 0, sizeof(*context));
    for (i = 0; i < 8; ++i) xx_blake2_node_init(context->leaves + i, i, false);
    context->state_tag = XX_BLAKE2SP_READY;
    return true;
}

bool xx_blake2sp_update(xx_blake2sp_context *context, const void *data, size_t size) {
    const uint8_t *input = (const uint8_t *)data;
    if (!context || context->state_tag != XX_BLAKE2SP_READY ||
        (!data && size) || (uint64_t)size > UINT64_MAX - context->total_size)
        return false;
    while (size) {
        unsigned slot = (unsigned)((context->total_size >> 6) & 7U);
        size_t take = 64U - (size_t)(context->total_size & 63U);
        if (take > size) take = size;
        xx_blake2_node_update(context->leaves + slot, input, take);
        context->total_size += take;
        input += take;
        size -= take;
    }
    return true;
}

bool xx_blake2sp_final(xx_blake2sp_context *context, uint8_t digest[32]) {
    xx_blake2sp_node root;
    uint8_t leaf_digest[32], result[32];
    unsigned i;
    if (!context || context->state_tag != XX_BLAKE2SP_READY || !digest) return false;
    xx_blake2_node_init(&root, 0, true);
    for (i = 0; i < 8; ++i) {
        xx_blake2_node_final(context->leaves + i, i == 7, leaf_digest);
        xx_blake2_node_update(&root, leaf_digest, sizeof(leaf_digest));
    }
    xx_blake2_node_final(&root, true, result);
    xx_blake2sp_clear(context);
    xx_blake2_wipe(&root, sizeof(root));
    xx_blake2_wipe(leaf_digest, sizeof(leaf_digest));
    xx_rt_memcpy(digest, result, sizeof(result));
    xx_blake2_wipe(result, sizeof(result));
    return true;
}

void xx_blake2sp_clear(xx_blake2sp_context *context) {
    if (context) xx_blake2_wipe(context, sizeof(*context));
}

bool xx_blake2sp_calc(const void *data, size_t size, uint8_t digest[32]) {
    xx_blake2sp_context context;
    bool ok;
    if (!digest || (!data && size)) return false;
    xx_blake2sp_init(&context);
    ok = xx_blake2sp_update(&context, data, size) && xx_blake2sp_final(&context, digest);
    xx_blake2sp_clear(&context);
    return ok;
}

bool xx_blake2sp_calc_device(xx_io_device *device, int64_t offset, int64_t size,
                            uint8_t digest[32], xx_pd_struct *pd) {
    xx_blake2sp_context context;
    uint8_t buffer[8192];
    int64_t total, remaining = size;
    int level;
    bool ok = false;
    if (!device || !device->read || !digest || offset < 0 || size < 0 ||
        offset > INT64_MAX - size || xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(device);
    if ((total >= 0 && (offset > total || size > total - offset)) ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    xx_blake2sp_init(&context);
    level = xx_pd_enter_level(pd, (uint64_t)size, "BLAKE2sp checksum");
    while (remaining) {
        size_t chunk = remaining > (int64_t)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t count;
        if (xx_pd_is_stopped(pd)) goto done;
        count = xx_io_read(device, buffer, chunk);
        if (count <= 0 || (size_t)count > chunk || xx_pd_is_stopped(pd) ||
            !xx_blake2sp_update(&context, buffer, (size_t)count)) goto done;
        remaining -= count;
        xx_pd_set_current(pd, level, (uint64_t)(size - remaining));
    }
    if (!xx_pd_is_stopped(pd)) ok = xx_blake2sp_final(&context, digest);
done:
    xx_blake2sp_clear(&context);
    xx_blake2_wipe(buffer, sizeof(buffer));
    xx_pd_leave_level(pd, level);
    return ok;
}
