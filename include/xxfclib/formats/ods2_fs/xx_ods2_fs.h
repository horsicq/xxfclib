/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ODS2_FS_READER_H
#define XX_ODS2_FS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_ods2_fs;
XXFC_API void xx_ods2_fs_init(xx_ods2_fs *, xx_io_device *, int64_t);
XXFC_API xx_ods2_fs *xx_ods2_fs_create(xx_io_device *, int64_t);
XXFC_API void xx_ods2_fs_destroy(xx_ods2_fs *);
XXFC_API void xx_ods2_fs_free(xx_ods2_fs *);
static inline Abstractformat *xx_ods2_fs_to_format(xx_ods2_fs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
