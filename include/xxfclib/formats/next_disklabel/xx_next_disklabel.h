/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_NEXT_DISKLABEL_READER_H
#define XX_NEXT_DISKLABEL_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_next_disklabel;
XXFC_API void xx_next_disklabel_init(xx_next_disklabel *,xx_io_device *,int64_t);
XXFC_API xx_next_disklabel *xx_next_disklabel_create(xx_io_device *,int64_t);
XXFC_API void xx_next_disklabel_destroy(xx_next_disklabel *);
XXFC_API void xx_next_disklabel_free(xx_next_disklabel *);
static inline Abstractformat *xx_next_disklabel_to_format(xx_next_disklabel *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
