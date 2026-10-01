/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/devkitPro/libogc/master/libogc/tpl.c
 * TPL tiled texture and palette components, standard GX I/IA/RGB/RGBA/CI/CMPR formats, one mip level. Checks tile-rounded data lengths. No pixel decoding, extended TPL or mip chains.
 */
#ifndef XX_NINTENDO_TPL_H
#define XX_NINTENDO_TPL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_tpl { Abstractformat format; } xx_nintendo_tpl;
XXFC_API void xx_nintendo_tpl_init(xx_nintendo_tpl *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_tpl *xx_nintendo_tpl_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_tpl_destroy(xx_nintendo_tpl *);
XXFC_API void xx_nintendo_tpl_free(xx_nintendo_tpl *);
XXFC_API bool xx_nintendo_tpl_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_tpl_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
