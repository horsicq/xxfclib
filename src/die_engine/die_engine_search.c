/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "die_engine_search.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/rt/xx_rt.h"
#include <limits.h>
#include <string.h>
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>
#define DIE_BATCH_SSE2 1
#endif

typedef struct {
    xx_data_signature signature;
    const xx_data_sig_record *anchor;
    int64_t prefix;
    int64_t limit;
    int next;
} BatchPattern;

#if DIE_BATCH_SSE2
#if (defined(__GNUC__) || defined(__clang__)) && defined(__i386__)
__attribute__((target("sse2")))
#endif
static int64_t next_anchor_sse2(const uint8_t *bytes, int64_t pos,
    int64_t end, const uint8_t *first, size_t count)
{
    size_t i;
    while (end - pos >= 16) {
        __m128i block = _mm_loadu_si128((const __m128i *)(bytes + pos));
        __m128i hits = _mm_setzero_si128();
        unsigned mask;
        for (i = 0; i < count; ++i)
            hits = _mm_or_si128(hits,
                _mm_cmpeq_epi8(block, _mm_set1_epi8((char)first[i])));
        mask = (unsigned)_mm_movemask_epi8(hits);
        if (mask) {
            while (!(mask & 1u)) { ++pos; mask >>= 1; }
            return pos;
        }
        pos += 16;
    }
    while (pos < end) {
        for (i = 0; i < count; ++i) if (bytes[pos] == first[i]) return pos;
        ++pos;
    }
    return end;
}
#endif

#if DIE_BATCH_SSE2
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif
static int64_t next_anchor_avx2(const uint8_t *bytes, int64_t pos,
    int64_t end, const uint8_t *table, const int *buckets)
{
    __m256i low_table = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)table));
    __m256i high_table = _mm256_broadcastsi128_si256(_mm_loadu_si128((const __m128i *)(table + 16)));
    __m256i bits = _mm256_setr_epi8(1,2,4,8,16,32,64,(char)128,1,2,4,8,16,32,64,(char)128,
        1,2,4,8,16,32,64,(char)128,1,2,4,8,16,32,64,(char)128);
    __m256i nibble_mask = _mm256_set1_epi8(15);
    while (end - pos >= 32) {
        __m256i block = _mm256_loadu_si256((const __m256i *)(bytes + pos));
        __m256i low = _mm256_and_si256(block, nibble_mask);
        __m256i high = _mm256_and_si256(_mm256_srli_epi16(block, 4), nibble_mask);
        __m256i membership = _mm256_blendv_epi8(_mm256_shuffle_epi8(low_table, low),
            _mm256_shuffle_epi8(high_table, low), _mm256_cmpgt_epi8(high, _mm256_set1_epi8(7)));
        __m256i bit = _mm256_shuffle_epi8(bits, high);
        unsigned mask = (unsigned)_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(_mm256_and_si256(membership, bit), bit));
        if (mask) {
            while (!(mask & 1u)) { ++pos; mask >>= 1; }
            return pos;
        }
        pos += 32;
    }
    while (pos < end && buckets[bytes[pos]] == -1) ++pos;
    return pos;
}
#endif

static int64_t next_anchor(const uint8_t *bytes, int64_t pos, int64_t end,
    const uint8_t *first, size_t count, const int *buckets, const uint8_t *table)
{
    if (buckets[bytes[pos]] != -1) return pos;
    if (count == 1) {
        const uint8_t *hit = memchr(bytes + pos, first[0], (size_t)(end - pos));
        return hit ? (int64_t)(hit - bytes) : end;
    }
#if DIE_BATCH_SSE2
    if (xx_is_avx2_enabled()) return next_anchor_avx2(bytes, pos, end, table, buckets);
    if (xx_is_sse2_enabled()) return next_anchor_sse2(bytes, pos, end, first, count);
#endif
    while (pos < end && buckets[bytes[pos]] == -1) ++pos;
    return pos;
}

bool die_find_signatures(const void *data, size_t data_size,
    int64_t offset, int64_t length, const char *const *patterns,
    size_t count, const xx_data_sig_context *context, int64_t *results)
{
    BatchPattern *compiled;
    int buckets[256], anchorless = -1;
    size_t i, budget = 0, pending = 0, first_count = 0;
    uint8_t first[256], table[32] = {0};
    int64_t end, scan_end, pos;
    bool ok = false;
    if (!results || count > DIE_SIGNATURE_BATCH_MAX) return false;
    for (i = 0; i < count; ++i) results[i] = -1;
    if (!patterns || data_size > INT64_MAX) return false;
    if (!count) return true;
    for (i = 0; i < count; ++i) {
        size_t size;
        if (!patterns[i]) return false;
        size = xx_rt_strlen(patterns[i]);
        if (size > DIE_SIGNATURE_BATCH_TEXT_MAX - budget) return false;
        budget += size;
    }
    if (!data || offset < 0 || (uint64_t)offset >= data_size) return true;
    if (length < 0 || length > (int64_t)data_size - offset)
        length = (int64_t)data_size - offset;
    if (!length) return true;
    if (count == 1 && data_size <= INT64_MAX - DIE_SIGNATURE_BATCH_TEXT_MAX) {
        results[0] = xx_data_signature_find_text(data, data_size, offset, length, patterns[0], context);
        return true;
    }
    compiled = xx_mem_calloc(count, sizeof(*compiled));
    if (!compiled) return false;
    for (i = 0; i < 256; ++i) buckets[i] = -1;
    end = offset + length;
    scan_end = end;
    for (i = 0; i < count; ++i) {
        char *normalized = xx_data_sig_normalize(patterns[i]);
        BatchPattern *p = &compiled[i];
        int j;
        if (!normalized) goto done;
        /* find_text deliberately uses records parsed before a syntax error. */
        (void)xx_data_signature_parse(&p->signature, normalized);
        xx_str_free(normalized);
        if (!p->signature.count) continue;
        ++pending;
        for (j = 0; j < p->signature.count; ++j) {
            const xx_data_sig_record *r = &p->signature.records[j];
            if (r->kind == XX_DATA_SIG_BYTES && r->data_size > 0) {
                p->anchor = r;
                break;
            }
            if (r->kind >= XX_DATA_SIG_SKIP && r->kind <= XX_DATA_SIG_ANSI_NUMBER) {
                if (r->window < 0 || r->window > INT64_MAX - p->prefix)
                    goto done;
                p->prefix += r->window;
            } else break;
        }
        if (p->anchor) {
            unsigned byte = p->anchor->data[0];
            /* Mirrors find_text's anchor search window, including its fixed
             * prefix extension. Full matching still uses the entire file. */
            p->limit = p->prefix > (int64_t)data_size - end
                ? (int64_t)data_size : end + p->prefix;
            if (p->limit > scan_end) scan_end = p->limit;
            if (buckets[byte] == -1) {
                first[first_count++] = (uint8_t)byte;
                table[(byte & 15u) + (byte >= 128 ? 16u : 0u)] |= (uint8_t)(1u << ((byte >> 4u) & 7u));
            }
            p->next = buckets[byte];
            buckets[byte] = (int)i;
        } else {
            p->next = anchorless;
            anchorless = (int)i;
        }
    }
    for (pos = offset; pos < scan_end && pending; ++pos) {
        int id;
        if (anchorless == -1) {
            pos = next_anchor(data, pos, scan_end, first, first_count, buckets, table);
            if (pos == scan_end) break;
        }
        /* Anchorless patterns keep the exact existing matcher semantics. */
        if (pos < end) {
            for (id = anchorless; id != -1; id = compiled[id].next) {
                BatchPattern *p = &compiled[id];
                if (results[id] == -1
                    && xx_data_signature_match(data, data_size, pos,
                        &p->signature, context, NULL)) {
                    results[id] = pos;
                    --pending;
                }
            }
        }
        for (id = buckets[((const uint8_t *)data)[pos]]; id != -1;
             id = compiled[id].next) {
            BatchPattern *p = &compiled[id];
            int64_t candidate;
            if (results[id] != -1 || pos < p->prefix
                || p->anchor->data_size > p->limit - pos) continue;
            candidate = pos - p->prefix;
            if (candidate < offset || candidate >= end) continue;
            if (xx_rt_memcmp((const uint8_t *)data + pos, p->anchor->data,
                    (size_t)p->anchor->data_size) == 0
                && xx_data_signature_match(data, data_size, candidate,
                    &p->signature, context, NULL)) {
                results[id] = candidate;
                --pending;
            }
        }
    }
    ok = true;
done:
    for (i = 0; i < count; ++i) xx_data_signature_free(&compiled[i].signature);
    xx_mem_free(compiled);
    if (!ok) for (i = 0; i < count; ++i) results[i] = -1;
    return ok;
}
