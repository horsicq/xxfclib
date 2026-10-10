/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ECMA67_FS_READER_H
#define XX_ECMA67_FS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_ecma67_fs;
XXFC_API void xx_ecma67_fs_init(xx_ecma67_fs *, xx_io_device *, int64_t);
XXFC_API xx_ecma67_fs *xx_ecma67_fs_create(xx_io_device *, int64_t);
XXFC_API void xx_ecma67_fs_destroy(xx_ecma67_fs *);
XXFC_API void xx_ecma67_fs_free(xx_ecma67_fs *);
static inline Abstractformat *xx_ecma67_fs_to_format(xx_ecma67_fs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
