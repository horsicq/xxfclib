/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_BYTE_TRANSFORM_H
#define XX_BYTE_TRANSFORM_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xxfclib/xxfc_defs.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_byte_transform_kind {
    XX_BYTE_FILL,
    XX_BYTE_XOR,
    XX_BYTE_AND,
    XX_BYTE_OR,
    XX_BYTE_NOT,
    XX_BYTE_ADD,
    XX_BYTE_SUBTRACT,
    XX_BYTE_ROTATE_LEFT,
    XX_BYTE_ROTATE_RIGHT,
    XX_BYTE_SWAP_ENDIAN
} xx_byte_transform_kind;
typedef struct xx_byte_transform {
    xx_byte_transform_kind kind;
    unsigned int width; /* 1, 2, 4 or 8 byte unsigned words. */
    bool big_endian;
    uint64_t operand;
    const unsigned char *pattern;
    size_t pattern_length;
} xx_byte_transform;
/* Allocation-free and transactional argument validation. Non-fill lengths
 * must contain complete words. Fill phase permits streaming repeated patterns.
 * Arithmetic wraps to width; rotates count modulo word bits. */
XXFC_API bool xx_byte_transform_apply(void *, size_t, uint64_t fill_phase, const xx_byte_transform *);
#ifdef __cplusplus
}
#endif
#endif
