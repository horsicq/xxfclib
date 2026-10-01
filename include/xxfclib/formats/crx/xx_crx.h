/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_CRX_H
#define XX_CRX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_crx { Abstractformat format; } xx_crx;
XXFC_API void xx_crx_init(xx_crx *,xx_io_device *,int64_t);
XXFC_API xx_crx *xx_crx_create(xx_io_device *,int64_t);
XXFC_API void xx_crx_destroy(xx_crx *);
XXFC_API void xx_crx_free(xx_crx *);
XXFC_API bool xx_crx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_crx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
