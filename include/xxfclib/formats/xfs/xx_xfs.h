/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_XFS_READER_H
#define XX_XFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_xfs;
XXFC_API void xx_xfs_init(xx_xfs *, xx_io_device *, int64_t);
XXFC_API xx_xfs *xx_xfs_create(xx_io_device *, int64_t);
XXFC_API void xx_xfs_destroy(xx_xfs *);
XXFC_API void xx_xfs_free(xx_xfs *);
static inline Abstractformat *xx_xfs_to_format(xx_xfs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
