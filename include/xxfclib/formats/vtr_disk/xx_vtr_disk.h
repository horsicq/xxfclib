/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only VTrucco indexed bitstream tracks; exact extraction scope is documented in the source.
 */
#ifndef XX_VTR_DISK_H
#define XX_VTR_DISK_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_sector_info xx_vtr_disk;
XXFC_API void xx_vtr_disk_init(xx_vtr_disk *, xx_io_device *, int64_t);
XXFC_API xx_vtr_disk *xx_vtr_disk_create(xx_io_device *, int64_t);
XXFC_API void xx_vtr_disk_destroy(xx_vtr_disk *);
XXFC_API void xx_vtr_disk_free(xx_vtr_disk *);
static inline Abstractformat *xx_vtr_disk_to_format(xx_vtr_disk *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
