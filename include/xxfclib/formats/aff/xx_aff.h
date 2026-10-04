/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_AFF_READER_H
#define XX_AFF_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_aff;
XXFC_API void xx_aff_init(xx_aff *,xx_io_device *,int64_t);
XXFC_API xx_aff *xx_aff_create(xx_io_device *,int64_t);
XXFC_API void xx_aff_destroy(xx_aff *);
XXFC_API void xx_aff_free(xx_aff *);
static inline Abstractformat *xx_aff_to_format(xx_aff *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
