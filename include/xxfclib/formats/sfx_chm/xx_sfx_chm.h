/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/documents/xchmarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_CHM_H
#define XX_SFX_CHM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_chm { Abstractformat format; } xx_sfx_chm;
XXFC_API void xx_sfx_chm_init(xx_sfx_chm *,xx_io_device *,int64_t);
XXFC_API xx_sfx_chm *xx_sfx_chm_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_chm_destroy(xx_sfx_chm *);
XXFC_API void xx_sfx_chm_free(xx_sfx_chm *);
XXFC_API bool xx_sfx_chm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_chm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
