/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_HPFS_READER_H
#define XX_HPFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_hpfs;
XXFC_API void xx_hpfs_init(xx_hpfs *,xx_io_device *,int64_t);
XXFC_API xx_hpfs *xx_hpfs_create(xx_io_device *,int64_t);
XXFC_API void xx_hpfs_destroy(xx_hpfs *);
XXFC_API void xx_hpfs_free(xx_hpfs *);
static inline Abstractformat *xx_hpfs_to_format(xx_hpfs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
