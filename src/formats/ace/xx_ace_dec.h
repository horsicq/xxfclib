/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_ACE_DEC_H
#define XX_ACE_DEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct xx_ace_history_s {
    uint8_t *dictionary;
    size_t dictionary_size;
    size_t position;
    size_t filled;
} xx_ace_history;
void xx_ace_history_clear(xx_ace_history *history);
bool xx_ace_history_append(xx_ace_history *history, const void *bytes,
                           size_t count, unsigned dictionary_bits);
bool xx_ace_decode_lzh(const void *source, size_t source_size, void *destination,
                       size_t destination_size, unsigned dictionary_bits);
bool xx_ace_decode_blocked(const void *source, size_t source_size, void *destination,
                           size_t destination_size, unsigned dictionary_bits);
bool xx_ace_decode_lzh_solid(const void *source, size_t source_size, void *destination,
                             size_t destination_size, unsigned dictionary_bits,
                             xx_ace_history *history);
bool xx_ace_decode_blocked_solid(const void *source, size_t source_size, void *destination,
                                 size_t destination_size, unsigned dictionary_bits,
                                 xx_ace_history *history);
#endif
