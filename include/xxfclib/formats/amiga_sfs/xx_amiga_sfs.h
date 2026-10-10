/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_AMIGA_SFS_READER_H
#define XX_AMIGA_SFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_amiga_sfs;
XXFC_API void xx_amiga_sfs_init(xx_amiga_sfs *, xx_io_device *, int64_t);
XXFC_API xx_amiga_sfs *xx_amiga_sfs_create(xx_io_device *, int64_t);
XXFC_API void xx_amiga_sfs_destroy(xx_amiga_sfs *);
XXFC_API void xx_amiga_sfs_free(xx_amiga_sfs *);
static inline Abstractformat *xx_amiga_sfs_to_format(xx_amiga_sfs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
