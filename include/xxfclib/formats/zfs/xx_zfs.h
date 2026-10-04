/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ZFS_READER_H
#define XX_ZFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_zfs;
XXFC_API void xx_zfs_init(xx_zfs *,xx_io_device *,int64_t);
XXFC_API xx_zfs *xx_zfs_create(xx_io_device *,int64_t);
XXFC_API void xx_zfs_destroy(xx_zfs *);
XXFC_API void xx_zfs_free(xx_zfs *);
static inline Abstractformat *xx_zfs_to_format(xx_zfs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
