/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://files.mpoli.fi/unpacked/software/programm/general/gcgpe10.zip/pcx.txt
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_PCX_H
#define XX_PCX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pcx { Abstractformat format; } xx_pcx;
XXFC_API void xx_pcx_init(xx_pcx *,xx_io_device *,int64_t);
XXFC_API xx_pcx *xx_pcx_create(xx_io_device *,int64_t);
XXFC_API void xx_pcx_destroy(xx_pcx *);
XXFC_API void xx_pcx_free(xx_pcx *);
XXFC_API bool xx_pcx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pcx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
