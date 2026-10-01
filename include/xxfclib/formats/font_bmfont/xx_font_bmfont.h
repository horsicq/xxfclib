/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://www.angelcode.com/products/bmfont/doc/file_format.html
 * Binary BMFont3 with complete info/common/pages/chars/kerning blocks, bounded atlas rectangles, unique scalar character IDs and page/kerning references (up to4096 characters/kerning pairs). Original typed blocks exported; text/XML variants, external texture loading and rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_FONT_BMFONT_H
#define XX_FONT_BMFONT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_font_bmfont { Abstractformat format; } xx_font_bmfont;
XXFC_API void xx_font_bmfont_init(xx_font_bmfont *,xx_io_device *,int64_t);
XXFC_API xx_font_bmfont *xx_font_bmfont_create(xx_io_device *,int64_t);
XXFC_API void xx_font_bmfont_destroy(xx_font_bmfont *);
XXFC_API void xx_font_bmfont_free(xx_font_bmfont *);
XXFC_API bool xx_font_bmfont_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_font_bmfont_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
