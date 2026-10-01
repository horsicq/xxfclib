/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_BZIP2_H
#define XX_SFX_BZIP2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_bzip2 { Abstractformat format; } xx_sfx_bzip2;
XXFC_API void xx_sfx_bzip2_init(xx_sfx_bzip2 *,xx_io_device *,int64_t);
XXFC_API xx_sfx_bzip2 *xx_sfx_bzip2_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_bzip2_destroy(xx_sfx_bzip2 *);
XXFC_API void xx_sfx_bzip2_free(xx_sfx_bzip2 *);
XXFC_API bool xx_sfx_bzip2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_bzip2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
