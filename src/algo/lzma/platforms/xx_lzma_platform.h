/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_LZMA_PLATFORM_H
#define XX_LZMA_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#define XX_LZMA_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define XX_LZMA_INLINE inline __attribute__((always_inline))
#else
#define XX_LZMA_INLINE inline
#endif

/* Internal codec services. Copies require nonoverlapping buffers. Matching
 * reads exactly the supplied extent, including at an allocation/page end. */
typedef struct xx_lzma_platform_s {
    const char *name;
    void (*copy)(uint8_t *destination, const uint8_t *source, size_t size);
    void (*fill_probs)(uint16_t *probabilities, size_t count);
    size_t (*match_length)(const uint8_t *first, const uint8_t *second, size_t maximum);
} xx_lzma_platform;

const xx_lzma_platform *xx_lzma_platform_select(void);

void xx_lzma_fill_probs_scalar(uint16_t *, size_t);
size_t xx_lzma_match_length_scalar(const uint8_t *, const uint8_t *, size_t);
void xx_lzma_fill_probs_sse2(uint16_t *, size_t);
size_t xx_lzma_match_length_sse2(const uint8_t *, const uint8_t *, size_t);
void xx_lzma_fill_probs_avx2(uint16_t *, size_t);
size_t xx_lzma_match_length_avx2(const uint8_t *, const uint8_t *, size_t);

#endif
