/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/freetype/freetype/master/src/bdf/bdflib.c
 * ASCII newline-terminated BDF2.1/2.2 horizontal bitmap fonts with complete traditional global/property/glyph grammar and single ENCODING values, bounded integral metrics and exact hexadecimal bitmap rows. Original glyph programs and global metadata remain encoded; other BDF2.2 metrics/secondary encodings, vertical metrics and rendering are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_FONT_BDF_H
#define XX_FONT_BDF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_font_bdf { Abstractformat format; } xx_font_bdf;
XXFC_API void xx_font_bdf_init(xx_font_bdf *,xx_io_device *,int64_t);
XXFC_API xx_font_bdf *xx_font_bdf_create(xx_io_device *,int64_t);
XXFC_API void xx_font_bdf_destroy(xx_font_bdf *);
XXFC_API void xx_font_bdf_free(xx_font_bdf *);
XXFC_API bool xx_font_bdf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_font_bdf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_font_bdf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_font_bdf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_font_bdf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
