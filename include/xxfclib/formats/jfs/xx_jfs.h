/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_JFS_READER_H
#define XX_JFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_jfs;
XXFC_API void xx_jfs_init(xx_jfs *,xx_io_device *,int64_t);
XXFC_API xx_jfs *xx_jfs_create(xx_io_device *,int64_t);
XXFC_API void xx_jfs_destroy(xx_jfs *);
XXFC_API void xx_jfs_free(xx_jfs *);
static inline Abstractformat *xx_jfs_to_format(xx_jfs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
