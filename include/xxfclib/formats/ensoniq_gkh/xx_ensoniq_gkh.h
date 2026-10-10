/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only GKH tagged sector image; exact extraction scope is documented in the source.
 */
#ifndef XX_ENSONIQ_GKH_H
#define XX_ENSONIQ_GKH_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_sector_info xx_ensoniq_gkh;
XXFC_API void xx_ensoniq_gkh_init(xx_ensoniq_gkh *, xx_io_device *, int64_t);
XXFC_API xx_ensoniq_gkh *xx_ensoniq_gkh_create(xx_io_device *, int64_t);
XXFC_API void xx_ensoniq_gkh_destroy(xx_ensoniq_gkh *);
XXFC_API void xx_ensoniq_gkh_free(xx_ensoniq_gkh *);
static inline Abstractformat *xx_ensoniq_gkh_to_format(xx_ensoniq_gkh *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
