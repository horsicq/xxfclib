/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_COFF_OBJECT_READER_H
#define XX_COFF_OBJECT_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_coff_object;
XXFC_API void xx_coff_object_init(xx_coff_object *, xx_io_device *, int64_t);
XXFC_API xx_coff_object *xx_coff_object_create(xx_io_device *, int64_t);
XXFC_API void xx_coff_object_destroy(xx_coff_object *);
XXFC_API void xx_coff_object_free(xx_coff_object *);
static inline Abstractformat *xx_coff_object_to_format(xx_coff_object *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
