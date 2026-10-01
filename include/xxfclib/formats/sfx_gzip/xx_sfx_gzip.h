/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_GZIP_H
#define XX_SFX_GZIP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_gzip { Abstractformat format; } xx_sfx_gzip;
XXFC_API void xx_sfx_gzip_init(xx_sfx_gzip *,xx_io_device *,int64_t);
XXFC_API xx_sfx_gzip *xx_sfx_gzip_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_gzip_destroy(xx_sfx_gzip *);
XXFC_API void xx_sfx_gzip_free(xx_sfx_gzip *);
XXFC_API bool xx_sfx_gzip_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_gzip_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
