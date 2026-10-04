/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_VMFS_READER_H
#define XX_VMFS_READER_H
#include "xxfclib/formats/volume/xx_volume.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_volume xx_vmfs;
XXFC_API void xx_vmfs_init(xx_vmfs *,xx_io_device *,int64_t);
XXFC_API xx_vmfs *xx_vmfs_create(xx_io_device *,int64_t);
XXFC_API void xx_vmfs_destroy(xx_vmfs *);
XXFC_API void xx_vmfs_free(xx_vmfs *);
static inline Abstractformat *xx_vmfs_to_format(xx_vmfs *r) { return r?&r->format:NULL; }
#ifdef __cplusplus
}
#endif
#endif
