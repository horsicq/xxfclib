/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_ACE_H
#define XX_SFX_ACE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_ace { Abstractformat format; void *internal; } xx_sfx_ace;
XXFC_API void xx_sfx_ace_init(xx_sfx_ace *,xx_io_device *,int64_t);
XXFC_API xx_sfx_ace *xx_sfx_ace_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_ace_destroy(xx_sfx_ace *);
XXFC_API void xx_sfx_ace_free(xx_sfx_ace *);
XXFC_API bool xx_sfx_ace_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_ace_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
