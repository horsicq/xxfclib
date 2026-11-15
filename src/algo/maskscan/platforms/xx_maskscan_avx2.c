/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_maskscan_avx2.c
 * @brief AVX2 masked probe search: 32 candidate positions per step.
 */

#include "xx_maskscan_platform.h"

#ifdef XX_MASKSCAN_X86

#define XX_MASKSCAN_AVX2_PROBE(k, at) \
    _mm256_cmpeq_epi8(_mm256_and_si256(_mm256_loadu_si256((const __m256i *)(const void *)(base[k] + (at))), \
                                       mask[k]), value[k])

/* One loop per probe count keeps every mask and value in a register. */
#define XX_MASKSCAN_AVX2_LOOP(test)                                     \
    while (last - position >= 63U) {                                    \
        __m256i low = test(position), high = test(position + 32U);      \
        if (!_mm256_testz_si256(_mm256_or_si256(low, high),             \
                                _mm256_or_si256(low, high))) {          \
            uint32_t bits = (uint32_t)_mm256_movemask_epi8(low);        \
            if (bits) return position + xx_maskscan_first_bit(bits);    \
            bits = (uint32_t)_mm256_movemask_epi8(high);                \
            return position + 32U + xx_maskscan_first_bit(bits);        \
        }                                                               \
        position += 64U;                                                \
        if (position > last) return SIZE_MAX;                           \
    }                                                                   \
    while (last - position >= 31U) {                                    \
        uint32_t bits = (uint32_t)_mm256_movemask_epi8(test(position)); \
        if (bits) return position + xx_maskscan_first_bit(bits);        \
        position += 32U;                                                \
        if (position > last) return SIZE_MAX;                           \
    }

#define XX_MASKSCAN_AVX2_TEST1(at) XX_MASKSCAN_AVX2_PROBE(0, at)
#define XX_MASKSCAN_AVX2_TEST2(at) _mm256_and_si256(XX_MASKSCAN_AVX2_TEST1(at), XX_MASKSCAN_AVX2_PROBE(1, at))
#define XX_MASKSCAN_AVX2_TEST3(at) _mm256_and_si256(XX_MASKSCAN_AVX2_TEST2(at), XX_MASKSCAN_AVX2_PROBE(2, at))
#define XX_MASKSCAN_AVX2_TEST4(at) _mm256_and_si256(XX_MASKSCAN_AVX2_TEST3(at), XX_MASKSCAN_AVX2_PROBE(3, at))

XX_MASKSCAN_TARGET_AVX2
size_t xx_maskscan_find_avx2(const uint8_t *data, size_t first, size_t last,
                             const xx_maskscan_probe *probes, size_t count)
{
    const uint8_t *base[XX_MASKSCAN_MAX_PROBES];
    __m256i mask[XX_MASKSCAN_MAX_PROBES], value[XX_MASKSCAN_MAX_PROBES];
    size_t position = first, k;
    for (k = 0; k < XX_MASKSCAN_MAX_PROBES; ++k) {
        size_t from = k < count ? k : 0;
        base[k] = data + probes[from].offset;
        mask[k] = _mm256_set1_epi8((char)probes[from].mask);
        value[k] = _mm256_set1_epi8((char)probes[from].value);
    }
    switch (count) {
    case 1: XX_MASKSCAN_AVX2_LOOP(XX_MASKSCAN_AVX2_TEST1) break;
    case 2: XX_MASKSCAN_AVX2_LOOP(XX_MASKSCAN_AVX2_TEST2) break;
    case 3: XX_MASKSCAN_AVX2_LOOP(XX_MASKSCAN_AVX2_TEST3) break;
    default: XX_MASKSCAN_AVX2_LOOP(XX_MASKSCAN_AVX2_TEST4) break;
    }
    return xx_maskscan_find_scalar(data, position, last, probes, count);
}

#endif /* XX_MASKSCAN_X86 */
