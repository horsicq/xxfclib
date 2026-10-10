/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only EDE-family sparse sector image; exact extraction scope is documented in the source.
 */
#ifndef XX_ENSONIQ_EDE_H
#define XX_ENSONIQ_EDE_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_sector_info xx_ensoniq_ede;
XXFC_API void xx_ensoniq_ede_init(xx_ensoniq_ede *, xx_io_device *, int64_t);
XXFC_API xx_ensoniq_ede *xx_ensoniq_ede_create(xx_io_device *, int64_t);
XXFC_API void xx_ensoniq_ede_destroy(xx_ensoniq_ede *);
XXFC_API void xx_ensoniq_ede_free(xx_ensoniq_ede *);
static inline Abstractformat *xx_ensoniq_ede_to_format(xx_ensoniq_ede *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
