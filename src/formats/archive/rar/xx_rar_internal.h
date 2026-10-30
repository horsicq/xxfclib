/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_RAR_INTERNAL_H
#define XXFCLIB_RAR_INTERNAL_H

/* Reader state shared by xx_rar.c and xx_rar_datastructs.c. */

#include "xxfclib/formats/archive/rar/xx_rar.h"

#include <stdint.h>

typedef struct xx_rar_block_s {
    xx_rar_version_t version;
    int64_t offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
    int64_t next_offset;
    uint64_t type;
    uint64_t flags;
    uint64_t extra_size;
    int64_t header_size_rel;
    int64_t type_rel;
    int64_t flags_rel;
    int64_t extra_size_rel;
    int64_t data_size_rel;
    uint8_t header_size_width;
    uint8_t type_width;
    uint8_t flags_width;
    uint8_t extra_size_width;
    uint8_t data_size_width;
} xx_rar_block;

bool xx_rar_parse_block(xx_io_device *device, int64_t device_size, xx_rar_version_t version, int64_t offset, xx_rar_block *block);

#endif /* XXFCLIB_RAR_INTERNAL_H */
