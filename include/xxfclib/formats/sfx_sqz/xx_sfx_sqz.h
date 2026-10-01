/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_SQZ_H
#define XX_SFX_SQZ_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/sqz/xx_sqz.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_sqz {
    Abstractformat format;
    xx_sqz *inner;
} xx_sfx_sqz;
XXFC_API void xx_sfx_sqz_init(xx_sfx_sqz *,xx_io_device *,int64_t);
XXFC_API xx_sfx_sqz *xx_sfx_sqz_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_sqz_destroy(xx_sfx_sqz *);
XXFC_API void xx_sfx_sqz_free(xx_sfx_sqz *);
XXFC_API bool xx_sfx_sqz_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_sqz_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
