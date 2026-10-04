/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_COHERENT_FS_READER_H
#define XX_COHERENT_FS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_coherent_fs;
XXFC_API void xx_coherent_fs_init(xx_coherent_fs *,xx_io_device *,int64_t);
XXFC_API xx_coherent_fs *xx_coherent_fs_create(xx_io_device *,int64_t);
XXFC_API void xx_coherent_fs_destroy(xx_coherent_fs *);
XXFC_API void xx_coherent_fs_free(xx_coherent_fs *);
static inline Abstractformat *xx_coherent_fs_to_format(xx_coherent_fs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
