/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_REFS_READER_H
#define XX_REFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_refs;
XXFC_API void xx_refs_init(xx_refs *,xx_io_device *,int64_t);
XXFC_API xx_refs *xx_refs_create(xx_io_device *,int64_t);
XXFC_API void xx_refs_destroy(xx_refs *);
XXFC_API void xx_refs_free(xx_refs *);
static inline Abstractformat *xx_refs_to_format(xx_refs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
