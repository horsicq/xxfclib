/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/emutos/emutos/master/include/fonthdr.h
 * Classic GEM/GDOS bitmap fonts with bounded endian-selected88-byte descriptor, character range, monotonic bit-offset table, optional horizontal offsets and exact scanline bitmap. Original font tables and raster exported; chained/extended/compressed fonts and rendering unsupported. Signatureless offset-zero detection only.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_FONT_GEM_FNT_H
#define XX_FONT_GEM_FNT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_font_gem_fnt {Abstractformat format;} xx_font_gem_fnt;
XXFC_API void xx_font_gem_fnt_init(xx_font_gem_fnt *,xx_io_device *,int64_t);
XXFC_API xx_font_gem_fnt *xx_font_gem_fnt_create(xx_io_device *,int64_t);
XXFC_API void xx_font_gem_fnt_destroy(xx_font_gem_fnt *);
XXFC_API void xx_font_gem_fnt_free(xx_font_gem_fnt *);
XXFC_API bool xx_font_gem_fnt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_font_gem_fnt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
