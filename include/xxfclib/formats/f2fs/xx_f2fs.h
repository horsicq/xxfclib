/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_F2FS_READER_H
#define XX_F2FS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_f2fs;
XXFC_API void xx_f2fs_init(xx_f2fs *, xx_io_device *, int64_t);
XXFC_API xx_f2fs *xx_f2fs_create(xx_io_device *, int64_t);
XXFC_API void xx_f2fs_destroy(xx_f2fs *);
XXFC_API void xx_f2fs_free(xx_f2fs *);
static inline Abstractformat *xx_f2fs_to_format(xx_f2fs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
