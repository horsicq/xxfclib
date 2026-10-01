/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xhaarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_HA_H
#define XX_SFX_HA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_ha { Abstractformat format; } xx_sfx_ha;
XXFC_API void xx_sfx_ha_init(xx_sfx_ha *,xx_io_device *,int64_t);
XXFC_API xx_sfx_ha *xx_sfx_ha_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_ha_destroy(xx_sfx_ha *);
XXFC_API void xx_sfx_ha_free(xx_sfx_ha *);
XXFC_API bool xx_sfx_ha_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_ha_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
