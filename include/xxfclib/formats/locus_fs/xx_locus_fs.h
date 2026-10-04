/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_LOCUS_FS_READER_H
#define XX_LOCUS_FS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_locus_fs;
XXFC_API void xx_locus_fs_init(xx_locus_fs *,xx_io_device *,int64_t);
XXFC_API xx_locus_fs *xx_locus_fs_create(xx_io_device *,int64_t);
XXFC_API void xx_locus_fs_destroy(xx_locus_fs *);
XXFC_API void xx_locus_fs_free(xx_locus_fs *);
static inline Abstractformat *xx_locus_fs_to_format(xx_locus_fs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
