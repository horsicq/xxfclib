/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xx_crc64_simd_internal.h"

#ifdef XX_CRC64_HAVE_VPCLMUL

XX_CRC64_TARGET_VPCLMUL
static XX_CRC64_INLINE __m256i xx_crc64_order256_vpclmul(__m256i v, bool reflected)
{
    if (reflected) {
        const __m256i m1 = _mm256_set1_epi8(0x55);
        const __m256i m2 = _mm256_set1_epi8(0x33);
        const __m256i m4 = _mm256_set1_epi8(0x0f);
        v = _mm256_or_si256(_mm256_and_si256(_mm256_srli_epi16(v, 1), m1), _mm256_slli_epi16(_mm256_and_si256(v, m1), 1));
        v = _mm256_or_si256(_mm256_and_si256(_mm256_srli_epi16(v, 2), m2), _mm256_slli_epi16(_mm256_and_si256(v, m2), 2));
        v = _mm256_or_si256(_mm256_and_si256(_mm256_srli_epi16(v, 4), m4), _mm256_slli_epi16(_mm256_and_si256(v, m4), 4));
    }
    return _mm256_shuffle_epi8(v, _mm256_setr_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0));
}

XX_CRC64_TARGET_VPCLMUL
static XX_CRC64_INLINE __m256i xx_crc64_fold256_vpclmul(__m256i state, __m256i powers)
{
    /* VPCLMULQDQ operates on both independent 128-bit lanes at once. */
    return _mm256_xor_si256(_mm256_clmulepi64_epi128(state, powers, 0x00), _mm256_clmulepi64_epi128(state, powers, 0x11));
}

XX_CRC64_TARGET_VPCLMUL
static uint64_t xx_crc64_vpclmul(uint64_t crc, const void *data, size_t size, bool reflected)
{
    const uint8_t *p = (const uint8_t *)data;
    const __m128i step16 = _mm_set_epi64x((long long)XX_CRC64_K192, (long long)XX_CRC64_K128);
    const __m256i step64 = _mm256_setr_epi64x((long long)XX_CRC64_K512, (long long)XX_CRC64_K576, (long long)XX_CRC64_K512, (long long)XX_CRC64_K576);
    __m256i ab, cd;
    __m128i state;
    uint64_t normal;
    if (!data || size < 64) {
        _mm256_zeroupper();
        return reflected ? xx_crc64_xz_sse2(crc, data, size) : xx_crc64_ecma_sse2(crc, data, size);
    }
    normal = reflected ? xx_crc64_reverse_bits(~crc) : crc;
    ab = xx_crc64_order256_vpclmul(_mm256_loadu_si256((const __m256i *)(const void *)p), reflected);
    cd = xx_crc64_order256_vpclmul(_mm256_loadu_si256((const __m256i *)(const void *)(p + 32)), reflected);
    ab = _mm256_xor_si256(ab, _mm256_set_epi64x(0, 0, (long long)normal, 0));
    p += 64;
    size -= 64;
    while (size >= 64) {
        ab = _mm256_xor_si256(xx_crc64_fold256_vpclmul(ab, step64), xx_crc64_order256_vpclmul(_mm256_loadu_si256((const __m256i *)(const void *)p), reflected));
        cd = _mm256_xor_si256(xx_crc64_fold256_vpclmul(cd, step64), xx_crc64_order256_vpclmul(_mm256_loadu_si256((const __m256i *)(const void *)(p + 32)), reflected));
        p += 64;
        size -= 64;
    }
    state = _mm_xor_si128(xx_crc64_fold128(_mm256_castsi256_si128(ab), step16), _mm256_extracti128_si256(ab, 1));
    state = _mm_xor_si128(xx_crc64_fold128(state, step16), _mm256_castsi256_si128(cd));
    state = _mm_xor_si128(xx_crc64_fold128(state, step16), _mm256_extracti128_si256(cd, 1));
    while (size >= 16) {
        state = _mm_xor_si128(xx_crc64_fold128(state, step16), xx_crc64_order128(_mm_loadu_si128((const __m128i *)(const void *)p), reflected));
        p += 16;
        size -= 16;
    }
    crc = xx_crc64_finish128(state, reflected);
    _mm256_zeroupper();
    return reflected ? xx_crc64_xz_scalar(crc, p, size) : xx_crc64_ecma_scalar(crc, p, size);
}

XX_CRC64_TARGET_VPCLMUL
uint64_t xx_crc64_xz_vpclmul(uint64_t crc, const void *data, size_t size)
{
    return xx_crc64_vpclmul(crc, data, size, true);
}

XX_CRC64_TARGET_VPCLMUL
uint64_t xx_crc64_ecma_vpclmul(uint64_t crc, const void *data, size_t size)
{
    return xx_crc64_vpclmul(crc, data, size, false);
}

#else

uint64_t xx_crc64_xz_vpclmul(uint64_t crc, const void *data, size_t size)
{
    return xx_crc64_xz_avx2(crc, data, size);
}

uint64_t xx_crc64_ecma_vpclmul(uint64_t crc, const void *data, size_t size)
{
    return xx_crc64_ecma_avx2(crc, data, size);
}

#endif
