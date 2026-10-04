/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_VXFS_READER_H
#define XX_VXFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_vxfs;
XXFC_API void xx_vxfs_init(xx_vxfs *,xx_io_device *,int64_t);
XXFC_API xx_vxfs *xx_vxfs_create(xx_io_device *,int64_t);
XXFC_API void xx_vxfs_destroy(xx_vxfs *);
XXFC_API void xx_vxfs_free(xx_vxfs *);
static inline Abstractformat *xx_vxfs_to_format(xx_vxfs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
