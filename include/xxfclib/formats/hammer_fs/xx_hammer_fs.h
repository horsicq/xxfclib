/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_HAMMER_FS_READER_H
#define XX_HAMMER_FS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_hammer_fs;
XXFC_API void xx_hammer_fs_init(xx_hammer_fs *,xx_io_device *,int64_t);
XXFC_API xx_hammer_fs *xx_hammer_fs_create(xx_io_device *,int64_t);
XXFC_API void xx_hammer_fs_destroy(xx_hammer_fs *);
XXFC_API void xx_hammer_fs_free(xx_hammer_fs *);
static inline Abstractformat *xx_hammer_fs_to_format(xx_hammer_fs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
