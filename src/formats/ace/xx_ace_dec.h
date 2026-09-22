/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_ACE_DEC_H
#define XX_ACE_DEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool xx_ace_decode_lzh(const void *source, size_t source_size, void *destination,
                       size_t destination_size, unsigned dictionary_bits);
bool xx_ace_decode_blocked(const void *source, size_t source_size, void *destination,
                           size_t destination_size, unsigned dictionary_bits);
#endif
