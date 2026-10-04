/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_LVM2_READER_H
#define XX_LVM2_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_lvm2;
XXFC_API void xx_lvm2_init(xx_lvm2 *,xx_io_device *,int64_t);
XXFC_API xx_lvm2 *xx_lvm2_create(xx_io_device *,int64_t);
XXFC_API void xx_lvm2_destroy(xx_lvm2 *);
XXFC_API void xx_lvm2_free(xx_lvm2 *);
static inline Abstractformat *xx_lvm2_to_format(xx_lvm2 *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
