/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_REISERFS_READER_H
#define XX_REISERFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_reiserfs;
XXFC_API void xx_reiserfs_init(xx_reiserfs *, xx_io_device *, int64_t);
XXFC_API xx_reiserfs *xx_reiserfs_create(xx_io_device *, int64_t);
XXFC_API void xx_reiserfs_destroy(xx_reiserfs *);
XXFC_API void xx_reiserfs_free(xx_reiserfs *);
static inline Abstractformat *xx_reiserfs_to_format(xx_reiserfs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
