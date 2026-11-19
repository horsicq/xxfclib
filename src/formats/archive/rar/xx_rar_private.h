/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reader-private declarations: shared by the reader's own units and, where
 * a DataStructs script needs reader state, src/datastructs/xx_xxds_readers.c.
 */
#ifndef XX_RAR_PRIVATE_H
#define XX_RAR_PRIVATE_H
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/rar/xx_rar.h"
#include "xx_rar_defs.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/aes/xx_aes.h"
#include "xxfclib/algo/blake2/xx_blake2.h"
#include "xxfclib/algo/rar/xx_rarx.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "../../../io/xx_io_policy.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "../../../io/platforms/xx_io_platform.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

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

#endif
