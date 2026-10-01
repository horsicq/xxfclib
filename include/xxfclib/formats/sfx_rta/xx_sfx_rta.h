/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_RTA_H
#define XX_SFX_RTA_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/rta/xx_rta.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_rta {
    Abstractformat format;
    xx_rta *inner;
} xx_sfx_rta;
XXFC_API void xx_sfx_rta_init(xx_sfx_rta *,xx_io_device *,int64_t);
XXFC_API xx_sfx_rta *xx_sfx_rta_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_rta_destroy(xx_sfx_rta *);
XXFC_API void xx_sfx_rta_free(xx_sfx_rta *);
XXFC_API bool xx_sfx_rta_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_rta_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
