/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_SOLAR_FS_READER_H
#define XX_SOLAR_FS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_solar_fs;
XXFC_API void xx_solar_fs_init(xx_solar_fs *,xx_io_device *,int64_t);
XXFC_API xx_solar_fs *xx_solar_fs_create(xx_io_device *,int64_t);
XXFC_API void xx_solar_fs_destroy(xx_solar_fs *);
XXFC_API void xx_solar_fs_free(xx_solar_fs *);
static inline Abstractformat *xx_solar_fs_to_format(xx_solar_fs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
