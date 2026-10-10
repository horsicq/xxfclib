/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_AMIGA_PFS_READER_H
#define XX_AMIGA_PFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_amiga_pfs;
XXFC_API void xx_amiga_pfs_init(xx_amiga_pfs *, xx_io_device *, int64_t);
XXFC_API xx_amiga_pfs *xx_amiga_pfs_create(xx_io_device *, int64_t);
XXFC_API void xx_amiga_pfs_destroy(xx_amiga_pfs *);
XXFC_API void xx_amiga_pfs_free(xx_amiga_pfs *);
static inline Abstractformat *xx_amiga_pfs_to_format(xx_amiga_pfs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
