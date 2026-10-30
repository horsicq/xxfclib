/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_GZ_INTERNAL_H
#define XXFCLIB_GZ_INTERNAL_H

/* Reader state shared by xx_gz.c and xx_gz_datastructs.c. */

#include <stdint.h>

typedef struct xx_gz_member_s {
    int64_t header_offset;
    int64_t data_offset;
    int64_t trailer_offset;
    int64_t member_end;
    int64_t header_size;
    int64_t compressed_size;
    int64_t extra_offset;
    int64_t extra_size;
    int64_t name_offset;
    int64_t name_size;
    int64_t comment_offset;
    int64_t comment_size;
    int64_t header_crc_offset;
    uint64_t uncompressed_size;
    uint32_t crc32;
    uint32_t modification_time;
    uint16_t header_crc16;
    uint8_t flags;
    uint8_t extra_flags;
    uint8_t operating_system;
    char *name;
    char *comment;
} xx_gz_member;

typedef struct xx_gz_private_s {
    xx_gz_member *members;
    size_t count;
    int64_t stream_end;
} xx_gz_private;

#endif /* XXFCLIB_GZ_INTERNAL_H */
