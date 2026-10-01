/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Gericom/EveryFileExplorer/master/3DS/NintendoWare/LYT1/CLYT.cs
 * CLYT version0x2020200, sequential bounded layout sections, balanced pane/group markers and bounded texture/font-name tables. Exports encoded sections; no UI execution, texture loading, material interpretation or rendering.
 */
#ifndef XX_NINTENDO_BCLYT_H
#define XX_NINTENDO_BCLYT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bclyt { Abstractformat format; } xx_nintendo_bclyt;
XXFC_API void xx_nintendo_bclyt_init(xx_nintendo_bclyt *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bclyt *xx_nintendo_bclyt_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bclyt_destroy(xx_nintendo_bclyt *);
XXFC_API void xx_nintendo_bclyt_free(xx_nintendo_bclyt *);
XXFC_API bool xx_nintendo_bclyt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bclyt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
