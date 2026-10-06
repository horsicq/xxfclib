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
 * @file xx_data_sse2.c
 * @brief SSE2 vectorized search implementation.
 */

#include "xx_data_platform.h"
#include "../xx_data_search_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/data/xx_pd.h"

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
#  if defined(_MSC_VER)
#    include <intrin.h>
#    include <emmintrin.h>
#  else
#    include <emmintrin.h>
#  endif
#endif

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
static inline size_t xx_prefix_emit_sse2(const uint32_t *masks, size_t mask_count,
                                       size_t base, size_t *positions,
                                       size_t capacity, size_t *next) {
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
            size_t offset = base + block * 16 + bit;
            positions[count++] = offset;
            if (count == capacity) {
                *next = offset + 1;
                return count;
            }
            mask &= mask - 1;
        }
    }
    *next = base + mask_count * 16;
    return count;
}
#endif

#if defined(__GNUC__) || defined(__clang__)
#  if defined(__i386__) && !defined(__SSE2__)
__attribute__((target("sse2")))
#  endif
#endif
size_t xx_data_collect_prefixes_sse2(const uint8_t *data, size_t size,
                                   size_t start, const uint8_t prefix[2],
                                   size_t *positions, size_t capacity, size_t *next) {
    if (next) *next = size;
    if (!data || !prefix || !positions || !next || !capacity ||
        size < 2 || start > size - 2) return 0;
#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
    __m128i first = _mm_set1_epi8((char)prefix[0]);
    __m128i second = _mm_set1_epi8((char)prefix[1]);
    while (size - start >= 129) {
        uint32_t masks[8];
        uint32_t any = 0;
        for (size_t block = 0; block < 8; ++block) {
            const uint8_t *p = data + start + block * 16;
            __m128i matches = _mm_and_si128(
                _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)p), first),
                _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(p + 1)), second));
            masks[block] = (uint32_t)_mm_movemask_epi8(matches);
            any |= masks[block];
        }
        if (any) return xx_prefix_emit_sse2(masks, 8, start, positions, capacity, next);
        start += 128;
    }
    while (size - start >= 17) {
        __m128i matches = _mm_and_si128(
            _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(data + start)), first),
            _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(data + start + 1)), second));
        uint32_t mask = (uint32_t)_mm_movemask_epi8(matches);
        if (mask) return xx_prefix_emit_sse2(&mask, 1, start, positions, capacity, next);
        start += 16;
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
#else
    return 0;
#endif
}

/* Emit complete batches for two independent raw anchor streams. */
#if (defined(__GNUC__) || defined(__clang__)) && defined(__i386__) && !defined(__SSE2__)
__attribute__((target("sse2")))
#endif
bool xx_data_collect_literal_dual_sse2(const uint8_t *data, size_t size,
                                       size_t start, const uint8_t prefix[2],
                                       XXDataLiteralDualBatch *batch) {
    if (!batch) return false;
    batch->adjacent_count = batch->skip_count = 0;
    batch->next = size;
    if (!data || !prefix || size < 2 || start > size - 2) return false;
#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
    __m128i first = _mm_set1_epi8((char)prefix[0]);
    __m128i second = _mm_set1_epi8((char)prefix[1]);
    while (size - start >= 130) {
        uint32_t adjacent[8], skip[8], any = 0;
        for (size_t block = 0; block < 8; ++block) {
            const uint8_t *p = data + start + block * 16;
            __m128i f = _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)p), first);
            adjacent[block] = (uint32_t)_mm_movemask_epi8(_mm_and_si128(f,
                _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(p + 1)), second)));
            skip[block] = (uint32_t)_mm_movemask_epi8(_mm_and_si128(f,
                _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(p + 2)), second)));
            any |= adjacent[block] | skip[block];
        }
        if (any) {
            size_t ignored;
            batch->adjacent_count = xx_prefix_emit_sse2(adjacent, 8, start, batch->adjacent, 128, &ignored);
            batch->skip_count = xx_prefix_emit_sse2(skip, 8, start, batch->skip, 128, &ignored);
            batch->next = start + 128;
            return true;
        }
        start += 128;
    }
    while (size - start >= 18) {
        __m128i f = _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(data + start)), first);
        uint32_t adjacent = (uint32_t)_mm_movemask_epi8(_mm_and_si128(f,
            _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(data + start + 1)), second)));
        uint32_t skip = (uint32_t)_mm_movemask_epi8(_mm_and_si128(f,
            _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(data + start + 2)), second)));
        if (adjacent || skip) {
            size_t ignored;
            batch->adjacent_count = xx_prefix_emit_sse2(&adjacent, 1, start, batch->adjacent, 128, &ignored);
            batch->skip_count = xx_prefix_emit_sse2(&skip, 1, start, batch->skip, 128, &ignored);
            batch->next = start + 16;
            return true;
        }
        start += 16;
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
#else
    return false;
#endif
}

/* Callers dispatch here only when xx_is_sse2_enabled(); 32-bit GCC/Clang
 * builds do not enable SSE2 globally, so enable it for this function. */
#if (defined(__GNUC__) || defined(__clang__)) && defined(__i386__) && !defined(__SSE2__)
__attribute__((target("sse2")))
#endif
int64_t xx_data_find_bytes_sse2(const uint8_t *pdata, size_t data_size, size_t start_offset, const uint8_t *pat, size_t pattern_size, xx_pd_struct *pd) {
    if (!pdata || !pat || pattern_size == 0 || start_offset + pattern_size > data_size || start_offset + pattern_size < start_offset) {
        return -1;
    }
    if (xx_pd_is_stopped(pd)) {
        return -1;
    }

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
    if (pattern_size == 1) {
        uint8_t target = pat[0];
        size_t limit = data_size - 1;
        size_t i = start_offset;
        __m128i target_v = _mm_set1_epi8((char)target);

        while (i + 16 <= data_size) {
            __m128i block = _mm_loadu_si128((const __m128i*)(pdata + i));
            int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(block, target_v));
            if (mask != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask);
#endif
                return (int64_t)(i + bit_idx);
            }
            i += 16;
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
        __m128i first_v = _mm_set1_epi8((char)pat[0]);
        __m128i last_v  = _mm_set1_epi8((char)pat[pattern_size - 1]);
        size_t i = start_offset;

        while (i + 16 <= limit + 1) {
            if (pd && (i & 0x3FFF) == 0 && xx_pd_is_stopped(pd)) {
                return -1;
            }
            __m128i b_first = _mm_loadu_si128((const __m128i*)(pdata + i));
            __m128i b_last  = _mm_loadu_si128((const __m128i*)(pdata + i + pattern_size - 1));
            int mask = _mm_movemask_epi8(_mm_and_si128(_mm_cmpeq_epi8(b_first, first_v), _mm_cmpeq_epi8(b_last, last_v)));

            while (mask != 0) {
                unsigned long bit_idx;
#if defined(_MSC_VER)
                _BitScanForward(&bit_idx, (unsigned long)mask);
#else
                bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask);
#endif
                size_t match_offset = i + bit_idx;
                if (pattern_size <= 2 || xx_mem_compare(pdata + match_offset + 1, pat + 1, pattern_size - 2) == 0) {
                    return (int64_t)match_offset;
                }
                mask &= mask - 1;
            }
            i += 16;
        }

        while (i <= limit) {
            if (pdata[i] == pat[0] && pdata[i + pattern_size - 1] == pat[pattern_size - 1]) {
                if (pattern_size <= 2 || xx_mem_compare(pdata + i + 1, pat + 1, pattern_size - 2) == 0) {
                    return (int64_t)i;
                }
            }
            i++;
        }
        return -1;
    }
#else
    (void)pdata; (void)data_size; (void)start_offset; (void)pat; (void)pattern_size;
    return -1;
#endif
}

static inline bool xx_masked_equal_sse2(const uint8_t *data, const uint8_t *value,
                                        const uint8_t *mask, size_t size) {
    size_t i;
    for (i = 0; i < size; ++i) {
        if ((uint8_t)(data[i] & mask[i]) != value[i]) return false;
    }
    return true;
}

/* Callers dispatch here only when xx_is_sse2_enabled(); 32-bit GCC/Clang
 * builds do not enable SSE2 globally, so enable it for this function. */
#if (defined(__GNUC__) || defined(__clang__)) && defined(__i386__) && !defined(__SSE2__)
__attribute__((target("sse2")))
#endif
int64_t xx_data_find_masked_sse2(const uint8_t *data, size_t size,
                                 const uint8_t *value, const uint8_t *mask,
                                 size_t pattern_size, size_t idx1, size_t idx2) {
    if (!data || !value || !mask || pattern_size == 0 || pattern_size > size ||
        idx1 >= pattern_size || idx2 >= pattern_size) {
        return -1;
    }

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))) || \
    ((defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__)))
    {
        size_t last = size - pattern_size;
        size_t p = 0;
        __m128i b1 = _mm_set1_epi8((char)value[idx1]);
        __m128i b2 = _mm_set1_epi8((char)value[idx2]);

        /* 16 starts per step; p + 15 <= last keeps both loads inside the data. */
        while (last >= 15 && p <= last - 15) {
            __m128i d1 = _mm_loadu_si128((const __m128i *)(const void *)(data + p + idx1));
            __m128i d2 = _mm_loadu_si128((const __m128i *)(const void *)(data + p + idx2));
            uint32_t bits = (uint32_t)_mm_movemask_epi8(
                _mm_and_si128(_mm_cmpeq_epi8(d1, b1), _mm_cmpeq_epi8(d2, b2)));

            while (bits) {
                unsigned long bit;
#if defined(_MSC_VER)
                _BitScanForward(&bit, (unsigned long)bits);
#else
                bit = (unsigned long)__builtin_ctz(bits);
#endif
                if (xx_masked_equal_sse2(data + p + bit, value, mask, pattern_size)) {
                    return (int64_t)(p + bit);
                }
                bits &= bits - 1U;
            }
            p += 16;
        }

        for (; p <= last; ++p) {
            if (data[p + idx1] == value[idx1] && data[p + idx2] == value[idx2] &&
                xx_masked_equal_sse2(data + p, value, mask, pattern_size)) {
                return (int64_t)p;
            }
        }
        return -1;
    }
#else
    (void)xx_masked_equal_sse2;
    return -1;
#endif
}
