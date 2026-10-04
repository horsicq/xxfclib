/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_HPOFS_READER_H
#define XX_HPOFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_hpofs;
XXFC_API void xx_hpofs_init(xx_hpofs *,xx_io_device *,int64_t);
XXFC_API xx_hpofs *xx_hpofs_create(xx_io_device *,int64_t);
XXFC_API void xx_hpofs_destroy(xx_hpofs *);
XXFC_API void xx_hpofs_free(xx_hpofs *);
static inline Abstractformat *xx_hpofs_to_format(xx_hpofs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
