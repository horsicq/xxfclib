/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Emax utility bank/sample wrapper; exact extraction scope is documented in the source.
 */
#ifndef XX_EMAX_DISK_H
#define XX_EMAX_DISK_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_sector_info xx_emax_disk;
XXFC_API void xx_emax_disk_init(xx_emax_disk *, xx_io_device *, int64_t);
XXFC_API xx_emax_disk *xx_emax_disk_create(xx_io_device *, int64_t);
XXFC_API void xx_emax_disk_destroy(xx_emax_disk *);
XXFC_API void xx_emax_disk_free(xx_emax_disk *);
static inline Abstractformat *xx_emax_disk_to_format(xx_emax_disk *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
