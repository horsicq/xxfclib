/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://nulib.com/library/FTN.e08002.htm
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_NUFX_H
#define XX_NUFX_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_nufx;
XXFC_API void xx_nufx_init(xx_nufx *,xx_io_device *,int64_t);
XXFC_API xx_nufx *xx_nufx_create(xx_io_device *,int64_t);
XXFC_API void xx_nufx_destroy(xx_nufx *);
XXFC_API void xx_nufx_free(xx_nufx *);
XXFC_API bool xx_nufx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nufx_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nufx_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
