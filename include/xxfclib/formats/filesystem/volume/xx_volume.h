/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_VOLUME_READER_H
#define XX_VOLUME_READER_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_volume {
    Abstractformat format;
    uint64_t block_size, blocks;
    uint32_t version;
    bool big_endian;
    char label[128];
    const char *capability;
    const xx_list_s *parse_options;
    bool identity_only;
    uint32_t logical_sector_size, heads, sectors_per_track;
} xx_volume;
XXFC_API void xx_volume_init(xx_volume *, xx_io_device *, int64_t, xx_file_type_t, const char *);
XXFC_API void xx_volume_destroy(xx_volume *);
/* CHS partition tables require geometry supplied by the enclosing image or
 * caller. Zero/invalid geometry is rejected; it is never guessed. */
XXFC_API bool xx_volume_set_geometry(xx_volume *, uint32_t, uint32_t, uint32_t);
#endif
