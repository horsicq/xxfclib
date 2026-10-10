/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_SGI_EFS_READER_H
#define XX_SGI_EFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_sgi_efs;
XXFC_API void xx_sgi_efs_init(xx_sgi_efs *, xx_io_device *, int64_t);
XXFC_API xx_sgi_efs *xx_sgi_efs_create(xx_io_device *, int64_t);
XXFC_API void xx_sgi_efs_destroy(xx_sgi_efs *);
XXFC_API void xx_sgi_efs_free(xx_sgi_efs *);
static inline Abstractformat *xx_sgi_efs_to_format(xx_sgi_efs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
