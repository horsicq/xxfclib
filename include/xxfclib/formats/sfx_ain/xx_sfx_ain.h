/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xainarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_AIN_H
#define XX_SFX_AIN_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_ain { Abstractformat format; } xx_sfx_ain;
XXFC_API void xx_sfx_ain_init(xx_sfx_ain *,xx_io_device *,int64_t);
XXFC_API xx_sfx_ain *xx_sfx_ain_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_ain_destroy(xx_sfx_ain *);
XXFC_API void xx_sfx_ain_free(xx_sfx_ain *);
XXFC_API bool xx_sfx_ain_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_ain_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
