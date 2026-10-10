/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_data_avx2.c
 * @brief AVX2 vectorized search implementation.
 */

#include "xx_data_platform.h"
#include "../xx_data_search_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/data/xx_pd.h"

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
#if defined(_MSC_VER)
#include <intrin.h>
#include <immintrin.h>
#else
#include <immintrin.h>
#endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#define XX_TARGET_AVX2 __attribute__((__target__("avx2")))
#else
#define XX_TARGET_AVX2
#endif

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))

static inline bool xx_fast_pattern_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    if (len <= 4) {
        if (len == 1) return a[0] == b[0];
        if (len == 2) {
            uint16_t v1, v2;
            xx_mem_copy(&v1, a, 2);
            xx_mem_copy(&v2, b, 2);
            return v1 == v2;
        }
        if (len == 3) {
            uint16_t v1, v2;
            xx_mem_copy(&v1, a, 2);
            xx_mem_copy(&v2, b, 2);
            return (v1 == v2) && (a[2] == b[2]);
        }
        uint32_t v1, v2;
        xx_mem_copy(&v1, a, 4);
        xx_mem_copy(&v2, b, 4);
        return v1 == v2;
    }
    if (len <= 8) {
        uint32_t v1a, v1b, v2a, v2b;
        xx_mem_copy(&v1a, a, 4);
        xx_mem_copy(&v2a, b, 4);
        xx_mem_copy(&v1b, a + len - 4, 4);
        xx_mem_copy(&v2b, b + len - 4, 4);
        return (v1a == v2a) && (v1b == v2b);
    }
    return xx_mem_compare(a, b, len) == 0;
}

static inline uint8_t xx_byte_weight(uint8_t c)
{
    if (c == 0x00) return 255;
    if (c == 0xFF) return 220;
    if (c == 0x20) return 180;
    if (c >= 'a' && c <= 'z') return 100;
    if (c >= 'A' && c <= 'Z') return 100;
    if (c >= '0' && c <= '9') return 120;
    return 50;
}

static inline void xx_select_filter_indices(const uint8_t *pat, size_t len, size_t *out_idx1, size_t *out_idx2)
{
    if (len <= 2) {
        *out_idx1 = 0;
        *out_idx2 = len - 1;
        return;
    }
    size_t i1 = 0, i2 = len - 1;
    uint8_t w1 = 255, w2 = 255;
    for (size_t k = 0; k < len; ++k) {
        uint8_t w = xx_byte_weight(pat[k]);
        if (w < w1) {
            w2 = w1;
            i2 = i1;
            w1 = w;
            i1 = k;
        } else if (w < w2 && k != i1) {
            w2 = w;
            i2 = k;
        }
    }
    if (i1 == i2) {
        i2 = (i1 == 0) ? (len - 1) : 0;
    }
    if (i1 > i2) {
        size_t tmp = i1;
        i1 = i2;
        i2 = tmp;
    }
    *out_idx1 = i1;
    *out_idx2 = i2;
}

bool xx_data_can_fuse_literal_prefix_avx2(const uint8_t *pat, size_t pattern_size)
{
    size_t first, second;
    if (!pat || pattern_size < 2) return false;
    xx_select_filter_indices(pat, pattern_size, &first, &second);
    return first == 0 && second == 1;
}

static inline size_t xx_prefix_emit_avx2(const uint32_t *masks, size_t mask_count, size_t base, size_t *positions, size_t capacity, size_t *next)
{
    size_t count = 0;
    for (size_t block = 0; block < mask_count; ++block) {
        uint32_t mask = masks[block];
        while (mask) {
            unsigned long bit;
#if defined(_MSC_VER)
            _BitScanForward(&bit, (unsigned long)mask);
#else
            bit = (unsigned long)__builtin_ctz(mask);
#endif
            size_t offset = base + block * 32 + bit;
            positions[count++] = offset;
            if (count == capacity) {
                *next = offset + 1;
                return count;
            }
            mask &= mask - 1;
        }
    }
    *next = base + mask_count * 32;
    return count;
}

XX_TARGET_AVX2
size_t xx_data_collect_prefixes_avx2(const uint8_t *data, size_t size, size_t start, const uint8_t prefix[2], size_t *positions, size_t capacity, size_t *next)
{
    if (next) *next = size;
    if (!data || !prefix || !positions || !next || !capacity || size < 2 || start > size - 2) return 0;
    __m256i first = _mm256_set1_epi8((char)prefix[0]);
    __m256i second = _mm256_set1_epi8((char)prefix[1]);

    /* One extra byte is required for the final adjacent-byte comparison. */
    while (size - start >= 129) {
        __m256i m0 = _mm256_and_si256(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start)), first),
                                      _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 1)), second));
        __m256i m1 = _mm256_and_si256(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 32)), first),
                                      _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 33)), second));
        __m256i m2 = _mm256_and_si256(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 64)), first),
                                      _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 65)), second));
        __m256i m3 = _mm256_and_si256(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 96)), first),
                                      _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 97)), second));
        __m256i any = _mm256_or_si256(_mm256_or_si256(m0, m1), _mm256_or_si256(m2, m3));
        if (!_mm256_testz_si256(any, any)) {
            uint32_t masks[4] = {(uint32_t)_mm256_movemask_epi8(m0), (uint32_t)_mm256_movemask_epi8(m1), (uint32_t)_mm256_movemask_epi8(m2),
                                 (uint32_t)_mm256_movemask_epi8(m3)};
            return xx_prefix_emit_avx2(masks, 4, start, positions, capacity, next);
        }
        start += 128;
    }
    while (size - start >= 33) {
        __m256i matches = _mm256_and_si256(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start)), first),
                                           _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 1)), second));
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(matches);
        if (mask) return xx_prefix_emit_avx2(&mask, 1, start, positions, capacity, next);
        start += 32;
    }
    size_t count = 0;
    while (start < size - 1) {
        if (data[start] == prefix[0] && data[start + 1] == prefix[1]) {
            positions[count++] = start;
            if (count == capacity) {
                *next = start + 1;
                return count;
            }
        }
        ++start;
    }
    *next = start;
    return count;
}

XX_TARGET_AVX2
bool xx_data_collect_literal_dual_avx2(const uint8_t *data, size_t size, size_t start, const uint8_t prefix[2], XXDataLiteralDualBatch *batch)
{
    if (!batch) return false;
    batch->adjacent_count = batch->skip_count = 0;
    batch->next = size;
    if (!data || !prefix || size < 2 || start > size - 2) return false;
    __m256i first = _mm256_set1_epi8((char)prefix[0]);
    __m256i second = _mm256_set1_epi8((char)prefix[1]);

    /* Byte shifts cannot cross a 128-bit lane; keep those starts eligible. */
    __m256i boundary = _mm256_set_epi64x((long long)0xff00000000000000ULL, 0, (long long)0xff00000000000000ULL, 0);

    /* Two extra bytes preserve both channels at the last SIMD lane. */
    while (size - start >= 130) {
        __m256i f0 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start)), first);
        __m256i f1 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 32)), first);
        __m256i f2 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 64)), first);
        __m256i f3 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 96)), first);
        __m256i b0 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 1)), second);
        __m256i b1 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 33)), second);
        __m256i b2 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 65)), second);
        __m256i b3 = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 97)), second);
        /* Reuse the adjacent-byte vectors to reject empty blocks conservatively.
           Possible matches still pass through the exact comparisons below. */
        __m256i g0 = _mm256_and_si256(f0, _mm256_or_si256(boundary, _mm256_or_si256(b0, _mm256_srli_si256(b0, 1))));
        __m256i g1 = _mm256_and_si256(f1, _mm256_or_si256(boundary, _mm256_or_si256(b1, _mm256_srli_si256(b1, 1))));
        __m256i g2 = _mm256_and_si256(f2, _mm256_or_si256(boundary, _mm256_or_si256(b2, _mm256_srli_si256(b2, 1))));
        __m256i g3 = _mm256_and_si256(f3, _mm256_or_si256(boundary, _mm256_or_si256(b3, _mm256_srli_si256(b3, 1))));
        __m256i coarse_any = _mm256_or_si256(_mm256_or_si256(g0, g1), _mm256_or_si256(g2, g3));
        if (_mm256_testz_si256(coarse_any, coarse_any)) {
            start += 128;
            continue;
        }
        __m256i a0 = _mm256_and_si256(f0, b0);
        __m256i a1 = _mm256_and_si256(f1, b1);
        __m256i a2 = _mm256_and_si256(f2, b2);
        __m256i a3 = _mm256_and_si256(f3, b3);
        __m256i s0 = _mm256_and_si256(f0, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 2)), second));
        __m256i s1 = _mm256_and_si256(f1, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 34)), second));
        __m256i s2 = _mm256_and_si256(f2, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 66)), second));
        __m256i s3 = _mm256_and_si256(f3, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 98)), second));
        __m256i adjacent_any = _mm256_or_si256(_mm256_or_si256(a0, a1), _mm256_or_si256(a2, a3));
        __m256i skip_any = _mm256_or_si256(_mm256_or_si256(s0, s1), _mm256_or_si256(s2, s3));
        __m256i any = _mm256_or_si256(adjacent_any, skip_any);
        if (!_mm256_testz_si256(any, any)) {
            uint32_t adjacent[4] = {(uint32_t)_mm256_movemask_epi8(a0), (uint32_t)_mm256_movemask_epi8(a1), (uint32_t)_mm256_movemask_epi8(a2),
                                    (uint32_t)_mm256_movemask_epi8(a3)};
            uint32_t skip[4] = {(uint32_t)_mm256_movemask_epi8(s0), (uint32_t)_mm256_movemask_epi8(s1), (uint32_t)_mm256_movemask_epi8(s2),
                                (uint32_t)_mm256_movemask_epi8(s3)};
            size_t ignored;
            batch->adjacent_count = xx_prefix_emit_avx2(adjacent, 4, start, batch->adjacent, 128, &ignored);
            batch->skip_count = xx_prefix_emit_avx2(skip, 4, start, batch->skip, 128, &ignored);
            batch->next = start + 128;
            return true;
        }
        start += 128;
    }
    while (size - start >= 34) {
        __m256i f = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start)), first);
        __m256i a = _mm256_and_si256(f, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 1)), second));
        __m256i s = _mm256_and_si256(f, _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(data + start + 2)), second));
        if (!_mm256_testz_si256(_mm256_or_si256(a, s), _mm256_or_si256(a, s))) {
            uint32_t adjacent = (uint32_t)_mm256_movemask_epi8(a);
            uint32_t skip = (uint32_t)_mm256_movemask_epi8(s);
            size_t ignored;
            batch->adjacent_count = xx_prefix_emit_avx2(&adjacent, 1, start, batch->adjacent, 128, &ignored);
            batch->skip_count = xx_prefix_emit_avx2(&skip, 1, start, batch->skip, 128, &ignored);
            batch->next = start + 32;
            return true;
        }
        start += 32;
    }
    while (start < size - 1) {
        if (data[start] == prefix[0]) {
            if (data[start + 1] == prefix[1]) batch->adjacent[batch->adjacent_count++] = start;
            if (size - start >= 3 && data[start + 2] == prefix[1]) batch->skip[batch->skip_count++] = start;
        }
        ++start;
    }
    batch->next = start;
    return batch->adjacent_count || batch->skip_count;
}

XX_TARGET_AVX2
int64_t xx_data_find_bytes_avx2(const uint8_t *pdata, size_t data_size, size_t start_offset, const uint8_t *pat, size_t pattern_size, xx_pd_struct *pd)
{
    if (!pdata || !pat || pattern_size == 0 || start_offset + pattern_size > data_size || start_offset + pattern_size < start_offset) {
        return -1;
    }
    if (xx_pd_is_stopped(pd)) {
        return -1;
    }

    if (pattern_size == 1) {
        uint8_t target = pat[0];
        size_t limit = data_size - 1;
        size_t i = start_offset;
        __m256i target_v = _mm256_set1_epi8((char)target);

        /* 4x unrolled 128-byte loop */
        while (i + 128 <= data_size) {
            if (pd && (i & 0x7FFF) == 0 && xx_pd_is_stopped(pd)) {
                return -1;
            }
            __m256i b0 = _mm256_loadu_si256((const __m256i *)(pdata + i));
            __m256i b1 = _mm256_loadu_si256((const __m256i *)(pdata + i + 32));
            __m256i b2 = _mm256_loadu_si256((const __m256i *)(pdata + i + 64));
            __m256i b3 = _mm256_loadu_si256((const __m256i *)(pdata + i + 96));

            __m256i m0 = _mm256_cmpeq_epi8(b0, target_v);
            __m256i m1 = _mm256_cmpeq_epi8(b1, target_v);
            __m256i m2 = _mm256_cmpeq_epi8(b2, target_v);
            __m256i m3 = _mm256_cmpeq_epi8(b3, target_v);

            __m256i any = _mm256_or_si256(_mm256_or_si256(m0, m1), _mm256_or_si256(m2, m3));
            if (_mm256_testz_si256(any, any)) {
                i += 128;
                continue;
            }

            unsigned int mask0 = (unsigned int)_mm256_movemask_epi8(m0);
            if (mask0 != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask0);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask0);
#endif
                return (int64_t)(i + bit_idx);
            }

            unsigned int mask1 = (unsigned int)_mm256_movemask_epi8(m1);
            if (mask1 != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask1);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask1);
#endif
                return (int64_t)(i + 32 + bit_idx);
            }

            unsigned int mask2 = (unsigned int)_mm256_movemask_epi8(m2);
            if (mask2 != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask2);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask2);
#endif
                return (int64_t)(i + 64 + bit_idx);
            }

            unsigned int mask3 = (unsigned int)_mm256_movemask_epi8(m3);
            if (mask3 != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask3);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask3);
#endif
                return (int64_t)(i + 96 + bit_idx);
            }
            i += 128;
        }

        while (i + 32 <= data_size) {
            __m256i block = _mm256_loadu_si256((const __m256i *)(pdata + i));
            unsigned int mask = (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(block, target_v));
            if (mask != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask);
#endif
                return (int64_t)(i + bit_idx);
            }
            i += 32;
        }

        while (i <= limit) {
            if (pdata[i] == target) {
                return (int64_t)i;
            }
            i++;
        }
        return -1;
    } else {
        size_t limit = data_size - pattern_size;
        size_t idx1, idx2;
        xx_select_filter_indices(pat, pattern_size, &idx1, &idx2);

        __m256i v1 = _mm256_set1_epi8((char)pat[idx1]);
        __m256i v2 = _mm256_set1_epi8((char)pat[idx2]);
        size_t i = start_offset;

        /* 4x unrolled 128-byte loop */
        while (i + 128 + idx2 <= data_size && i + 128 <= limit + 1) {
            if (pd && (i & 0x7FFF) == 0 && xx_pd_is_stopped(pd)) {
                return -1;
            }
            __m256i f0 = _mm256_loadu_si256((const __m256i *)(pdata + i + idx1));
            __m256i l0 = _mm256_loadu_si256((const __m256i *)(pdata + i + idx2));
            __m256i f1 = _mm256_loadu_si256((const __m256i *)(pdata + i + 32 + idx1));
            __m256i l1 = _mm256_loadu_si256((const __m256i *)(pdata + i + 32 + idx2));
            __m256i f2 = _mm256_loadu_si256((const __m256i *)(pdata + i + 64 + idx1));
            __m256i l2 = _mm256_loadu_si256((const __m256i *)(pdata + i + 64 + idx2));
            __m256i f3 = _mm256_loadu_si256((const __m256i *)(pdata + i + 96 + idx1));
            __m256i l3 = _mm256_loadu_si256((const __m256i *)(pdata + i + 96 + idx2));

            __m256i m0 = _mm256_and_si256(_mm256_cmpeq_epi8(f0, v1), _mm256_cmpeq_epi8(l0, v2));
            __m256i m1 = _mm256_and_si256(_mm256_cmpeq_epi8(f1, v1), _mm256_cmpeq_epi8(l1, v2));
            __m256i m2 = _mm256_and_si256(_mm256_cmpeq_epi8(f2, v1), _mm256_cmpeq_epi8(l2, v2));
            __m256i m3 = _mm256_and_si256(_mm256_cmpeq_epi8(f3, v1), _mm256_cmpeq_epi8(l3, v2));

            __m256i any = _mm256_or_si256(_mm256_or_si256(m0, m1), _mm256_or_si256(m2, m3));
            if (_mm256_testz_si256(any, any)) {
                i += 128;
                continue;
            }

            unsigned int mask0 = (unsigned int)_mm256_movemask_epi8(m0);
            while (mask0 != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask0);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask0);
#endif
                size_t off = i + bit_idx;
                if (off <= limit && (pattern_size <= 2 || xx_fast_pattern_equal(pdata + off, pat, pattern_size))) {
                    return (int64_t)off;
                }
                mask0 &= mask0 - 1;
            }

            unsigned int mask1 = (unsigned int)_mm256_movemask_epi8(m1);
            while (mask1 != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask1);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask1);
#endif
                size_t off = i + 32 + bit_idx;
                if (off <= limit && (pattern_size <= 2 || xx_fast_pattern_equal(pdata + off, pat, pattern_size))) {
                    return (int64_t)off;
                }
                mask1 &= mask1 - 1;
            }

            unsigned int mask2 = (unsigned int)_mm256_movemask_epi8(m2);
            while (mask2 != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask2);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask2);
#endif
                size_t off = i + 64 + bit_idx;
                if (off <= limit && (pattern_size <= 2 || xx_fast_pattern_equal(pdata + off, pat, pattern_size))) {
                    return (int64_t)off;
                }
                mask2 &= mask2 - 1;
            }

            unsigned int mask3 = (unsigned int)_mm256_movemask_epi8(m3);
            while (mask3 != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask3);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask3);
#endif
                size_t off = i + 96 + bit_idx;
                if (off <= limit && (pattern_size <= 2 || xx_fast_pattern_equal(pdata + off, pat, pattern_size))) {
                    return (int64_t)off;
                }
                mask3 &= mask3 - 1;
            }

            i += 128;
        }

        /* 32-byte loop */
        while (i + 32 + idx2 <= data_size && i + 32 <= limit + 1) {
            if (pd && (i & 0x7FFF) == 0 && xx_pd_is_stopped(pd)) {
                return -1;
            }
            __m256i b_first = _mm256_loadu_si256((const __m256i *)(pdata + i + idx1));
            __m256i b_last = _mm256_loadu_si256((const __m256i *)(pdata + i + idx2));
            unsigned int mask = (unsigned int)_mm256_movemask_epi8(_mm256_and_si256(_mm256_cmpeq_epi8(b_first, v1), _mm256_cmpeq_epi8(b_last, v2)));

            while (mask != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask);
#endif
                size_t off = i + bit_idx;
                if (off <= limit && (pattern_size <= 2 || xx_fast_pattern_equal(pdata + off, pat, pattern_size))) {
                    return (int64_t)off;
                }
                mask &= mask - 1;
            }
            i += 32;
        }

        /* Scalar tail */
        while (i <= limit) {
            if (pdata[i + idx1] == pat[idx1] && pdata[i + idx2] == pat[idx2]) {
                if (pattern_size <= 2 || xx_fast_pattern_equal(pdata + i, pat, pattern_size)) {
                    return (int64_t)i;
                }
            }
            i++;
        }
        return -1;
    }
}

static inline bool xx_masked_equal_avx2(const uint8_t *data, const uint8_t *value, const uint8_t *mask, size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i) {
        if ((uint8_t)(data[i] & mask[i]) != value[i]) return false;
    }
    return true;
}

XX_TARGET_AVX2
int64_t xx_data_find_masked_avx2(const uint8_t *data, size_t size, const uint8_t *value, const uint8_t *mask, size_t pattern_size, size_t idx1, size_t idx2)
{
    size_t last;
    size_t p = 0;
    __m256i b1;
    __m256i b2;

    if (!data || !value || !mask || pattern_size == 0 || pattern_size > size || idx1 >= pattern_size || idx2 >= pattern_size) {
        return -1;
    }

    last = size - pattern_size;
    b1 = _mm256_set1_epi8((char)value[idx1]);
    b2 = _mm256_set1_epi8((char)value[idx2]);

    /* 32 starts per step. p + 31 <= last keeps both loads inside the data:
     * p + idx + 31 <= size - pattern_size + idx <= size - 1. */
    while (last >= 31 && p <= last - 31) {
        __m256i d1 = _mm256_loadu_si256((const __m256i *)(const void *)(data + p + idx1));
        __m256i d2 = _mm256_loadu_si256((const __m256i *)(const void *)(data + p + idx2));
        uint32_t bits = (uint32_t)_mm256_movemask_epi8(_mm256_and_si256(_mm256_cmpeq_epi8(d1, b1), _mm256_cmpeq_epi8(d2, b2)));

        while (bits) {
            unsigned long bit;
#if defined(_MSC_VER)
            _BitScanForward(&bit, (unsigned long)bits);
#else
            bit = (unsigned long)__builtin_ctz(bits);
#endif
            if (xx_masked_equal_avx2(data + p + bit, value, mask, pattern_size)) {
                return (int64_t)(p + bit);
            }
            bits &= bits - 1U;
        }
        p += 32;
    }

    for (; p <= last; ++p) {
        if (data[p + idx1] == value[idx1] && data[p + idx2] == value[idx2] && xx_masked_equal_avx2(data + p, value, mask, pattern_size)) {
            return (int64_t)p;
        }
    }
    return -1;
}
#else
int64_t xx_data_find_masked_avx2(const uint8_t *data, size_t size, const uint8_t *value, const uint8_t *mask, size_t pattern_size, size_t idx1, size_t idx2)
{
    (void)data;
    (void)size;
    (void)value;
    (void)mask;
    (void)pattern_size;
    (void)idx1;
    (void)idx2;
    return -1;
}
bool xx_data_collect_literal_dual_avx2(const uint8_t *data, size_t size, size_t start, const uint8_t prefix[2], XXDataLiteralDualBatch *batch)
{
    (void)data;
    (void)start;
    (void)prefix;
    if (batch) {
        batch->adjacent_count = batch->skip_count = 0;
        batch->next = size;
    }
    return false;
}
bool xx_data_can_fuse_literal_prefix_avx2(const uint8_t *pat, size_t pattern_size)
{
    (void)pat;
    (void)pattern_size;
    return false;
}
size_t xx_data_collect_prefixes_avx2(const uint8_t *data, size_t size, size_t start, const uint8_t prefix[2], size_t *positions, size_t capacity, size_t *next)
{
    (void)data;
    (void)start;
    (void)prefix;
    (void)positions;
    (void)capacity;
    if (next) *next = size;
    return 0;
}
int64_t xx_data_find_bytes_avx2(const uint8_t *pdata, size_t data_size, size_t start_offset, const uint8_t *pat, size_t pattern_size, xx_pd_struct *pd)
{
    (void)pdata;
    (void)data_size;
    (void)start_offset;
    (void)pat;
    (void)pattern_size;
    (void)pd;
    return -1;
}
#endif
