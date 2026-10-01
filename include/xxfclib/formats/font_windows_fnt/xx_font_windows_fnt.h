/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/freetype/freetype/master/src/winfonts/winfnt.c
 * Standalone Windows FNT2/3 raster fonts with complete glyph directory, bounded column-major bitmap extents and device/face strings. Original glyphs and font metadata are exported; vector fonts, .FON/NE wrappers and rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_FONT_WINDOWS_FNT_H
#define XX_FONT_WINDOWS_FNT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_font_windows_fnt { Abstractformat format; } xx_font_windows_fnt;
XXFC_API void xx_font_windows_fnt_init(xx_font_windows_fnt *,xx_io_device *,int64_t);
XXFC_API xx_font_windows_fnt *xx_font_windows_fnt_create(xx_io_device *,int64_t);
XXFC_API void xx_font_windows_fnt_destroy(xx_font_windows_fnt *);
XXFC_API void xx_font_windows_fnt_free(xx_font_windows_fnt *);
XXFC_API bool xx_font_windows_fnt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_font_windows_fnt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
