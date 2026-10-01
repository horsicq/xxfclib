/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_GXL_H
#define XX_SFX_GXL_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/gxl/xx_gxl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_gxl {
    Abstractformat format;
    xx_gxl *inner;
} xx_sfx_gxl;
XXFC_API void xx_sfx_gxl_init(xx_sfx_gxl *,xx_io_device *,int64_t);
XXFC_API xx_sfx_gxl *xx_sfx_gxl_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_gxl_destroy(xx_sfx_gxl *);
XXFC_API void xx_sfx_gxl_free(xx_sfx_gxl *);
XXFC_API bool xx_sfx_gxl_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_gxl_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
