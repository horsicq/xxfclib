/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xsqxarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_SQX_H
#define XX_SFX_SQX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_sqx { Abstractformat format; } xx_sfx_sqx;
XXFC_API void xx_sfx_sqx_init(xx_sfx_sqx *,xx_io_device *,int64_t);
XXFC_API xx_sfx_sqx *xx_sfx_sqx_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_sqx_destroy(xx_sfx_sqx *);
XXFC_API void xx_sfx_sqx_free(xx_sfx_sqx *);
XXFC_API bool xx_sfx_sqx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_sqx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
