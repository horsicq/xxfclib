/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/Algos/xdearkmodule_lha_p.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_PMARC_SFX_H
#define XX_PMARC_SFX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pmarc_sfx { Abstractformat format; } xx_pmarc_sfx;
XXFC_API void xx_pmarc_sfx_init(xx_pmarc_sfx *,xx_io_device *,int64_t);
XXFC_API xx_pmarc_sfx *xx_pmarc_sfx_create(xx_io_device *,int64_t);
XXFC_API void xx_pmarc_sfx_destroy(xx_pmarc_sfx *);
XXFC_API void xx_pmarc_sfx_free(xx_pmarc_sfx *);
XXFC_API bool xx_pmarc_sfx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pmarc_sfx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
