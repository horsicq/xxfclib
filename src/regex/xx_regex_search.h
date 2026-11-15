/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_REGEX_SEARCH_H
#define XX_REGEX_SEARCH_H

#include <stddef.h>

/* Find the first candidate whose bytes at 0 and offset match. Both reads
 * remain inside size, including vector loads and the final partial block.
 * This is only a necessary-condition filter; the matcher verifies candidates. */
const unsigned char *xx_regex_find_pair(const unsigned char *text, size_t size,
                                       unsigned char first, size_t offset,
                                       unsigned char second);

/* One byte position of a start filter: the set of bytes allowed at offset
 * from a candidate start, kept as a bitmap and as nibble tables for the
 * vector searches. Built once per compiled regex; read-only afterwards. */
typedef struct {
    size_t offset;
    unsigned char map[32];   /* bit (b & 7) of map[b >> 3] */
    unsigned char low[16];   /* bit h of low[n]: byte (h << 4) | n, h < 8 */
    unsigned char high[16];  /* bit h - 8 of high[n]: byte (h << 4) | n, h >= 8 */
    unsigned char bytes[3];  /* the members when there are at most three */
    unsigned char has_high;  /* a member is at or above 0x80 */
    unsigned members;        /* 0 .. 256 */
} xx_regex_byteset;

#define XX_REGEX_MAX_SETS 4

void xx_regex_byteset_init(xx_regex_byteset *set, const unsigned char map[32], size_t offset);

/* First candidate p < candidates such that text[p + sets[j].offset] is in
 * sets[j] for every j < count (1 .. XX_REGEX_MAX_SETS), or NULL. The caller
 * guarantees every tested byte is readable: candidates - 1 + the largest
 * offset is inside the subject. Necessary-condition filter only. */
const unsigned char *xx_regex_find_sets(const unsigned char *text, size_t candidates,
                                       const xx_regex_byteset *sets, size_t count);

#endif
