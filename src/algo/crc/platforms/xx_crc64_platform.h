/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_CRC64_PLATFORM_H
#define XX_CRC64_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Private implementations have the same incremental seed/finalization as the
 * public CRC64 functions. SIMD entry points require PCLMULQDQ in addition to
 * their named ISA; call them only after checking the selected platform. */
typedef struct xx_crc64_platform_s {
    const char *name;
    uint64_t (*xz)(uint64_t, const void *, size_t);
    uint64_t (*ecma)(uint64_t, const void *, size_t);
} xx_crc64_platform;

bool xx_crc64_has_pclmul(void);
const xx_crc64_platform *xx_crc64_platform_select(void);
uint64_t xx_crc64_xz_scalar(uint64_t, const void *, size_t);
uint64_t xx_crc64_ecma_scalar(uint64_t, const void *, size_t);
uint64_t xx_crc64_xz_sse2(uint64_t, const void *, size_t);
uint64_t xx_crc64_ecma_sse2(uint64_t, const void *, size_t);
uint64_t xx_crc64_xz_avx2(uint64_t, const void *, size_t);
uint64_t xx_crc64_ecma_avx2(uint64_t, const void *, size_t);

#endif
