/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_RT11_FS_READER_H
#define XX_RT11_FS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_rt11_fs;
XXFC_API void xx_rt11_fs_init(xx_rt11_fs *,xx_io_device *,int64_t);
XXFC_API xx_rt11_fs *xx_rt11_fs_create(xx_io_device *,int64_t);
XXFC_API void xx_rt11_fs_destroy(xx_rt11_fs *);
XXFC_API void xx_rt11_fs_free(xx_rt11_fs *);
static inline Abstractformat *xx_rt11_fs_to_format(xx_rt11_fs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
