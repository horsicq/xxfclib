/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reader-private declarations: shared by the reader's own units and, where
 * a DataStructs script needs reader state, src/datastructs/xx_xxds_readers.c.
 */
#ifndef XX_GZ_PRIVATE_H
#define XX_GZ_PRIVATE_H
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/archive/gz/xx_gz.h"
#include "xx_gz_defs.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "../../../algo/deflate/xx_deflate_internal.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

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

#endif
