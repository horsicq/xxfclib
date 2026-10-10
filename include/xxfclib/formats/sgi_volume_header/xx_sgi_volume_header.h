/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_SGI_VOLUME_HEADER_READER_H
#define XX_SGI_VOLUME_HEADER_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_sgi_volume_header;
XXFC_API void xx_sgi_volume_header_init(xx_sgi_volume_header *, xx_io_device *, int64_t);
XXFC_API xx_sgi_volume_header *xx_sgi_volume_header_create(xx_io_device *, int64_t);
XXFC_API void xx_sgi_volume_header_destroy(xx_sgi_volume_header *);
XXFC_API void xx_sgi_volume_header_free(xx_sgi_volume_header *);
static inline Abstractformat *xx_sgi_volume_header_to_format(xx_sgi_volume_header *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
