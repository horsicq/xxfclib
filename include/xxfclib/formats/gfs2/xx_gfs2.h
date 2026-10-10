/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_GFS2_READER_H
#define XX_GFS2_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_gfs2;
XXFC_API void xx_gfs2_init(xx_gfs2 *, xx_io_device *, int64_t);
XXFC_API xx_gfs2 *xx_gfs2_create(xx_io_device *, int64_t);
XXFC_API void xx_gfs2_destroy(xx_gfs2 *);
XXFC_API void xx_gfs2_free(xx_gfs2 *);
static inline Abstractformat *xx_gfs2_to_format(xx_gfs2 *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
