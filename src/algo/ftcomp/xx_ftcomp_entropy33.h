/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_FTCOMP_ENTROPY33_H
#define XX_FTCOMP_ENTROPY33_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Decode one fT33 entropy block into LZ token bytes.  initial_position is
 * the current LZ history count before this block (0xfba at member start). */
bool xx_ftcomp_entropy33_decode(const uint8_t *input, size_t input_size,
                                uint8_t *tokens, size_t expected,
                                size_t capacity, uint32_t initial_position,
                                size_t *produced, size_t *consumed);

#endif
