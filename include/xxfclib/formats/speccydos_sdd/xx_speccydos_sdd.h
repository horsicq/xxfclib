/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only SpeccyDOS side-sequential sectors; exact extraction scope is documented in the source.
 */
#ifndef XX_SPECCYDOS_SDD_H
#define XX_SPECCYDOS_SDD_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_sector_info xx_speccydos_sdd;
XXFC_API void xx_speccydos_sdd_init(xx_speccydos_sdd *, xx_io_device *, int64_t);
XXFC_API xx_speccydos_sdd *xx_speccydos_sdd_create(xx_io_device *, int64_t);
XXFC_API void xx_speccydos_sdd_destroy(xx_speccydos_sdd *);
XXFC_API void xx_speccydos_sdd_free(xx_speccydos_sdd *);
static inline Abstractformat *xx_speccydos_sdd_to_format(xx_speccydos_sdd *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
