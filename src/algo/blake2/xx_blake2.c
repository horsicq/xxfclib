/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original scalar BLAKE2sp implementation of the algorithm specified at
 * https://www.blake2.net/blake2.pdf (section 2.11) and RFC 7693.
 * The eight independent leaves are evaluated sequentially, without threads.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/blake2/xx_blake2.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "../../global/xx_read_buffer_internal.h"

#define XX_BLAKE2SP_READY UINT32_C(0x32535042)

static const uint32_t xx_blake2_initial[8] = {UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
                                              UINT32_C(0x510e527f), UINT32_C(0x9b05688c), UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)};

/* Message-word permutation, a fixed part of the BLAKE2 specification. */
static const uint8_t xx_blake2_permutation[10][16] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
                                                      {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4}, {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
                                                      {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13}, {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
                                                      {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11}, {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
                                                      {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5}, {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0}};

static void xx_blake2_wipe(void *data, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size) {
        *p++ = 0;
        --size;
    }
}

static inline uint32_t xx_blake2_rotr(uint32_t word, unsigned bits)
{
    return (word >> bits) | (word << (32U - bits));
}

#define XX_BLAKE2_G(a, b, c, d, first, second) do { \
    (a) += (b) + (first); \
    (d) = xx_blake2_rotr((d) ^ (a), 16); \
    (c) += (d); \
    (b) = xx_blake2_rotr((b) ^ (c), 12); \
    (a) += (b) + (second); \
    (d) = xx_blake2_rotr((d) ^ (a), 8); \
    (c) += (d); \
    (b) = xx_blake2_rotr((b) ^ (c), 7); \
} while (0)

#define XX_BLAKE2_ROUND(r) do { \
    const uint8_t *s = xx_blake2_permutation[r]; \
    XX_BLAKE2_G(work[0], work[4], work[8],  work[12], message[s[0]],  message[s[1]]);  \
    XX_BLAKE2_G(work[1], work[5], work[9],  work[13], message[s[2]],  message[s[3]]);  \
    XX_BLAKE2_G(work[2], work[6], work[10], work[14], message[s[4]],  message[s[5]]);  \
    XX_BLAKE2_G(work[3], work[7], work[11], work[15], message[s[6]],  message[s[7]]);  \
    XX_BLAKE2_G(work[0], work[5], work[10], work[15], message[s[8]],  message[s[9]]);  \
    XX_BLAKE2_G(work[1], work[6], work[11], work[12], message[s[10]], message[s[11]]); \
    XX_BLAKE2_G(work[2], work[7], work[8],  work[13], message[s[12]], message[s[13]]); \
    XX_BLAKE2_G(work[3], work[4], work[9],  work[14], message[s[14]], message[s[15]]); \
} while (0)

static void xx_blake2_compress(xx_blake2sp_node *node, bool final, bool last_node)
{
    uint32_t message[16], work[16];
    unsigned i;
    for (i = 0; i < 16; ++i) {
        message[i] = xx_mem_load32(node->pending + 4U * i);
    }
    for (i = 0; i < 8; ++i) {
        work[i] = node->words[i];
        work[i + 8U] = xx_blake2_initial[i];
    }
    work[12] ^= (uint32_t)node->compressed_size;
    work[13] ^= (uint32_t)(node->compressed_size >> 32);
    if (final) work[14] = ~work[14];
    if (final && last_node) work[15] = ~work[15];

    XX_BLAKE2_ROUND(0);
    XX_BLAKE2_ROUND(1);
    XX_BLAKE2_ROUND(2);
    XX_BLAKE2_ROUND(3);
    XX_BLAKE2_ROUND(4);
    XX_BLAKE2_ROUND(5);
    XX_BLAKE2_ROUND(6);
    XX_BLAKE2_ROUND(7);
    XX_BLAKE2_ROUND(8);
    XX_BLAKE2_ROUND(9);

    for (i = 0; i < 8; ++i) node->words[i] ^= work[i] ^ work[i + 8U];
}

static void xx_blake2_node_init(xx_blake2sp_node *node, unsigned index, bool root)
{
    xx_rt_memset(node, 0, sizeof(*node));
    xx_rt_memcpy(node->words, xx_blake2_initial, sizeof(node->words));
    /* Fixed digest=32, key=0, fanout=8, depth=2, leaf length=0,
     * node offset=index, node depth=0/1, inner digest length=32. */
    node->words[0] ^= UINT32_C(0x02080020);
    node->words[2] ^= (uint32_t)index;
    node->words[3] ^= UINT32_C(0x20000000) | (root ? UINT32_C(0x00010000) : 0U);
}

static void xx_blake2_node_update(xx_blake2sp_node *node, const uint8_t *data, size_t size)
{
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

static void xx_blake2_node_final(xx_blake2sp_node *node, bool last_node, uint8_t digest[32])
{
    unsigned i;
    node->compressed_size += node->pending_size;
    xx_rt_memset(node->pending + node->pending_size, 0, sizeof(node->pending) - node->pending_size);
    xx_blake2_compress(node, true, last_node);
    for (i = 0; i < 8; ++i) xx_mem_store32(digest + 4U * i, node->words[i]);
}

bool xx_blake2sp_init(xx_blake2sp_context *context)
{
    unsigned i;
    if (!context) return false;
    xx_rt_memset(context, 0, sizeof(*context));
    for (i = 0; i < 8; ++i) xx_blake2_node_init(context->leaves + i, i, false);
    context->state_tag = XX_BLAKE2SP_READY;
    return true;
}

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#define XX_BLAKE2_X86
#include <immintrin.h>
#endif
#if (defined(__GNUC__) || defined(__clang__)) && defined(XX_BLAKE2_X86)
#define XX_TARGET_AVX2 __attribute__((target("avx2")))
#else
#define XX_TARGET_AVX2
#endif

#ifdef XX_BLAKE2_X86
XX_TARGET_AVX2
static inline __m256i xx_blake2_rotr12_avx2(__m256i x) {
    return _mm256_or_si256(_mm256_srli_epi32(x, 12), _mm256_slli_epi32(x, 20));
}
XX_TARGET_AVX2
static inline __m256i xx_blake2_rotr7_avx2(__m256i x) {
    return _mm256_or_si256(_mm256_srli_epi32(x, 7), _mm256_slli_epi32(x, 25));
}

#define XX_BLAKE2_G_AVX2(a, b, c, d, x, y) do { \
    (a) = _mm256_add_epi32(_mm256_add_epi32((a), (b)), (x)); \
    (d) = _mm256_shuffle_epi8(_mm256_xor_si256((d), (a)), mask16); \
    (c) = _mm256_add_epi32((c), (d)); \
    (b) = xx_blake2_rotr12_avx2(_mm256_xor_si256((b), (c))); \
    (a) = _mm256_add_epi32(_mm256_add_epi32((a), (b)), (y)); \
    (d) = _mm256_shuffle_epi8(_mm256_xor_si256((d), (a)), mask8); \
    (c) = _mm256_add_epi32((c), (d)); \
    (b) = xx_blake2_rotr7_avx2(_mm256_xor_si256((b), (c))); \
} while (0)

#define XX_BLAKE2_ROUND_AVX2(r) do { \
    const uint8_t *s = xx_blake2_permutation[r]; \
    XX_BLAKE2_G_AVX2(v[0], v[4], v[8],  v[12], m[s[0]],  m[s[1]]);  \
    XX_BLAKE2_G_AVX2(v[1], v[5], v[9],  v[13], m[s[2]],  m[s[3]]);  \
    XX_BLAKE2_G_AVX2(v[2], v[6], v[10], v[14], m[s[4]],  m[s[5]]);  \
    XX_BLAKE2_G_AVX2(v[3], v[7], v[11], v[15], m[s[6]],  m[s[7]]);  \
    XX_BLAKE2_G_AVX2(v[0], v[5], v[10], v[15], m[s[8]],  m[s[9]]);  \
    XX_BLAKE2_G_AVX2(v[1], v[6], v[11], v[12], m[s[10]], m[s[11]]); \
    XX_BLAKE2_G_AVX2(v[2], v[7], v[8],  v[13], m[s[12]], m[s[13]]); \
    XX_BLAKE2_G_AVX2(v[3], v[4], v[9],  v[14], m[s[14]], m[s[15]]); \
} while (0)

XX_TARGET_AVX2
static size_t xx_blake2sp_update_blocks_avx2(xx_blake2sp_node *leaves, const uint8_t *input, size_t size)
{
    const __m256i mask16 = _mm256_setr_epi8(
        2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13,
        2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13
    );
    const __m256i mask8 = _mm256_setr_epi8(
        1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12,
        1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12
    );
    __m256i st[8];
    unsigned i;
    for (i = 0; i < 8; ++i) {
        st[i] = _mm256_setr_epi32(
            (int)leaves[0].words[i], (int)leaves[1].words[i],
            (int)leaves[2].words[i], (int)leaves[3].words[i],
            (int)leaves[4].words[i], (int)leaves[5].words[i],
            (int)leaves[6].words[i], (int)leaves[7].words[i]
        );
    }
    uint64_t c_size = leaves[0].compressed_size;
    const uint8_t *orig_input = input;

#define XX_TRANSPOSE_8x8_32(r0, r1, r2, r3, r4, r5, r6, r7, c0, c1, c2, c3, c4, c5, c6, c7) do { \
    __m256i _t0 = _mm256_unpacklo_epi32((r0), (r1)); \
    __m256i _t1 = _mm256_unpackhi_epi32((r0), (r1)); \
    __m256i _t2 = _mm256_unpacklo_epi32((r2), (r3)); \
    __m256i _t3 = _mm256_unpackhi_epi32((r2), (r3)); \
    __m256i _t4 = _mm256_unpacklo_epi32((r4), (r5)); \
    __m256i _t5 = _mm256_unpackhi_epi32((r4), (r5)); \
    __m256i _t6 = _mm256_unpacklo_epi32((r6), (r7)); \
    __m256i _t7 = _mm256_unpackhi_epi32((r6), (r7)); \
    __m256i _u0 = _mm256_unpacklo_epi64(_t0, _t2); \
    __m256i _u1 = _mm256_unpackhi_epi64(_t0, _t2); \
    __m256i _u2 = _mm256_unpacklo_epi64(_t1, _t3); \
    __m256i _u3 = _mm256_unpackhi_epi64(_t1, _t3); \
    __m256i _u4 = _mm256_unpacklo_epi64(_t4, _t6); \
    __m256i _u5 = _mm256_unpackhi_epi64(_t4, _t6); \
    __m256i _u6 = _mm256_unpacklo_epi64(_t5, _t7); \
    __m256i _u7 = _mm256_unpackhi_epi64(_t5, _t7); \
    (c0) = _mm256_permute2x128_si256(_u0, _u4, 0x20); \
    (c4) = _mm256_permute2x128_si256(_u0, _u4, 0x31); \
    (c1) = _mm256_permute2x128_si256(_u1, _u5, 0x20); \
    (c5) = _mm256_permute2x128_si256(_u1, _u5, 0x31); \
    (c2) = _mm256_permute2x128_si256(_u2, _u6, 0x20); \
    (c6) = _mm256_permute2x128_si256(_u2, _u6, 0x31); \
    (c3) = _mm256_permute2x128_si256(_u3, _u7, 0x20); \
    (c7) = _mm256_permute2x128_si256(_u3, _u7, 0x31); \
} while (0)

#define XX_BLAKE2SP_DO_BLOCK_AVX2(ptr0, ptr1, ptr2, ptr3, ptr4, ptr5, ptr6, ptr7) do { \
    c_size += 64U; \
    __m256i v[16]; \
    __m256i m[16]; \
    for (i = 0; i < 8; ++i) { \
        v[i] = st[i]; \
        v[i + 8] = _mm256_set1_epi32((int)xx_blake2_initial[i]); \
    } \
    v[12] = _mm256_xor_si256(v[12], _mm256_set1_epi32((int)(uint32_t)c_size)); \
    v[13] = _mm256_xor_si256(v[13], _mm256_set1_epi32((int)(uint32_t)(c_size >> 32))); \
    __m256i r0 = _mm256_loadu_si256((const __m256i *)(ptr0)); \
    __m256i r1 = _mm256_loadu_si256((const __m256i *)(ptr1)); \
    __m256i r2 = _mm256_loadu_si256((const __m256i *)(ptr2)); \
    __m256i r3 = _mm256_loadu_si256((const __m256i *)(ptr3)); \
    __m256i r4 = _mm256_loadu_si256((const __m256i *)(ptr4)); \
    __m256i r5 = _mm256_loadu_si256((const __m256i *)(ptr5)); \
    __m256i r6 = _mm256_loadu_si256((const __m256i *)(ptr6)); \
    __m256i r7 = _mm256_loadu_si256((const __m256i *)(ptr7)); \
    XX_TRANSPOSE_8x8_32(r0, r1, r2, r3, r4, r5, r6, r7, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7]); \
    r0 = _mm256_loadu_si256((const __m256i *)((const uint8_t *)(ptr0) + 32)); \
    r1 = _mm256_loadu_si256((const __m256i *)((const uint8_t *)(ptr1) + 32)); \
    r2 = _mm256_loadu_si256((const __m256i *)((const uint8_t *)(ptr2) + 32)); \
    r3 = _mm256_loadu_si256((const __m256i *)((const uint8_t *)(ptr3) + 32)); \
    r4 = _mm256_loadu_si256((const __m256i *)((const uint8_t *)(ptr4) + 32)); \
    r5 = _mm256_loadu_si256((const __m256i *)((const uint8_t *)(ptr5) + 32)); \
    r6 = _mm256_loadu_si256((const __m256i *)((const uint8_t *)(ptr6) + 32)); \
    r7 = _mm256_loadu_si256((const __m256i *)((const uint8_t *)(ptr7) + 32)); \
    XX_TRANSPOSE_8x8_32(r0, r1, r2, r3, r4, r5, r6, r7, m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]); \
    XX_BLAKE2_ROUND_AVX2(0); \
    XX_BLAKE2_ROUND_AVX2(1); \
    XX_BLAKE2_ROUND_AVX2(2); \
    XX_BLAKE2_ROUND_AVX2(3); \
    XX_BLAKE2_ROUND_AVX2(4); \
    XX_BLAKE2_ROUND_AVX2(5); \
    XX_BLAKE2_ROUND_AVX2(6); \
    XX_BLAKE2_ROUND_AVX2(7); \
    XX_BLAKE2_ROUND_AVX2(8); \
    XX_BLAKE2_ROUND_AVX2(9); \
    for (i = 0; i < 8; ++i) { \
        st[i] = _mm256_xor_si256(st[i], _mm256_xor_si256(v[i], v[i + 8])); \
    } \
} while (0)

    if (leaves[0].pending_size == 64U) {
        XX_BLAKE2SP_DO_BLOCK_AVX2(
            leaves[0].pending, leaves[1].pending, leaves[2].pending, leaves[3].pending,
            leaves[4].pending, leaves[5].pending, leaves[6].pending, leaves[7].pending
        );
        for (i = 0; i < 8; ++i) {
            leaves[i].pending_size = 0;
        }
    }

    while (size >= 1024U) {
        XX_BLAKE2SP_DO_BLOCK_AVX2(
            input + 0 * 64U, input + 1 * 64U, input + 2 * 64U, input + 3 * 64U,
            input + 4 * 64U, input + 5 * 64U, input + 6 * 64U, input + 7 * 64U
        );
        input += 512U;
        size -= 512U;
    }

    if (size >= 512U) {
        for (i = 0; i < 8; ++i) {
            xx_mem_copy(leaves[i].pending, input + i * 64U, 64U);
            leaves[i].pending_size = 64U;
        }
        input += 512U;
        size -= 512U;
    }

    for (i = 0; i < 8; ++i) {
        uint32_t out_words[8];
        _mm256_storeu_si256((__m256i *)out_words, st[i]);
        unsigned l;
        for (l = 0; l < 8; ++l) {
            leaves[l].words[i] = out_words[l];
            leaves[l].compressed_size = c_size;
        }
    }
    _mm256_zeroupper();

    return (size_t)(input - orig_input);
}
#endif

bool xx_blake2sp_update(xx_blake2sp_context *context, const void *data, size_t size)
{
    const uint8_t *input = (const uint8_t *)data;
    if (!context || context->state_tag != XX_BLAKE2SP_READY || (!data && size) || (uint64_t)size > UINT64_MAX - context->total_size) return false;

    while (size && (context->total_size & 511U) != 0) {
        unsigned slot = (unsigned)((context->total_size >> 6) & 7U);
        size_t take = 64U - (size_t)(context->total_size & 63U);
        if (take > size) take = size;
        xx_blake2_node_update(context->leaves + slot, input, take);
        context->total_size += take;
        input += take;
        size -= take;
    }

    if ((context->total_size & 511U) == 0) {
#ifdef XX_BLAKE2_X86
        if (xx_is_avx2_enabled() && size >= 512U) {
            size_t consumed = xx_blake2sp_update_blocks_avx2(context->leaves, input, size);
            context->total_size += consumed;
            input += consumed;
            size -= consumed;
        } else
#endif
        while (size >= 512U) {
            unsigned leaf;
            bool had_pending = (context->leaves[0].pending_size == 64U);
            if (had_pending) {
                for (leaf = 0; leaf < 8U; ++leaf) {
                    xx_blake2sp_node *node = context->leaves + leaf;
                    node->compressed_size += 64U;
                    xx_blake2_compress(node, false, false);
                }
            }
            for (leaf = 0; leaf < 8U; ++leaf) {
                xx_mem_copy(context->leaves[leaf].pending, input + leaf * 64U, 64U);
                context->leaves[leaf].pending_size = 64U;
            }
            context->total_size += 512U;
            input += 512U;
            size -= 512U;
        }
    }

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

bool xx_blake2sp_final(xx_blake2sp_context *context, uint8_t digest[32])
{
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

void xx_blake2sp_clear(xx_blake2sp_context *context)
{
    if (context) xx_blake2_wipe(context, sizeof(*context));
}

bool xx_blake2sp_calc(const void *data, size_t size, uint8_t digest[32])
{
    xx_blake2sp_context context;
    bool ok;
    if (!digest || (!data && size)) return false;
    xx_blake2sp_init(&context);
    ok = xx_blake2sp_update(&context, data, size) && xx_blake2sp_final(&context, digest);
    xx_blake2sp_clear(&context);
    return ok;
}

bool xx_blake2sp_calc_device(xx_io_device *device, int64_t offset, int64_t size, uint8_t digest[32], xx_pd_struct *pd)
{
    xx_blake2sp_context context;
    uint8_t *buffer;
    size_t capacity = 0;
    int64_t total, remaining = size;
    int level;
    bool ok = false;
    if (!device || !device->read || !digest || offset < 0 || size < 0 || offset > INT64_MAX - size || xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(device);
    if ((total >= 0 && (offset > total || size > total - offset)) || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    buffer = (uint8_t *)xx_read_buffer_alloc(size ? (uint64_t)size : 1U, &capacity);
    if (!buffer) return false;
    xx_blake2sp_init(&context);
    level = xx_pd_enter_level(pd, (uint64_t)size, "BLAKE2sp checksum");
    while (remaining) {
        size_t chunk = (uint64_t)remaining > capacity ? capacity : (size_t)remaining;
        ssize_t count;
        if (xx_pd_is_stopped(pd)) goto done;
        count = xx_io_read(device, buffer, chunk);
        if (count <= 0 || (size_t)count > chunk || xx_pd_is_stopped(pd) || !xx_blake2sp_update(&context, buffer, (size_t)count)) goto done;
        remaining -= count;
        xx_pd_set_current(pd, level, (uint64_t)(size - remaining));
    }
    if (!xx_pd_is_stopped(pd)) ok = xx_blake2sp_final(&context, digest);
done:
    xx_blake2sp_clear(&context);
    xx_blake2_wipe(buffer, capacity);
    xx_mem_free(buffer);
    xx_pd_leave_level(pd, level);
    return ok;
}
