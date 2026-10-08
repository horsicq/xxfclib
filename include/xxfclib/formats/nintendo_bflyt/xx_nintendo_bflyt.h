/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/exelix11/SwitchThemeInjector/master/SwitchThemesNX/source/SwitchThemesCommon/Layouts/Bflyt/Bflyt.cpp
 * FLYT version0x8040000, sequential bounded layout sections, balanced pane/group markers and bounded texture/font-name tables. Exports encoded sections; no UI execution, texture loading, material interpretation or rendering.
 */
#ifndef XX_NINTENDO_BFLYT_H
#define XX_NINTENDO_BFLYT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bflyt { Abstractformat format; } xx_nintendo_bflyt;
XXFC_API void xx_nintendo_bflyt_init(xx_nintendo_bflyt *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bflyt *xx_nintendo_bflyt_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bflyt_destroy(xx_nintendo_bflyt *);
XXFC_API void xx_nintendo_bflyt_free(xx_nintendo_bflyt *);
XXFC_API bool xx_nintendo_bflyt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bflyt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_bflyt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_bflyt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_bflyt_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
