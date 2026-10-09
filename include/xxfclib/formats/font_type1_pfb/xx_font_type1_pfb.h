/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://www.adobe.com/content/dam/acom/en/devnet/font/pdfs/T1_SPEC.pdf
 * Type1 PFB complete ASCII/binary/ASCII segment ordering and final marker, bounded segment sizes, font program identity, eexec transition and ASCII closing program. Original encrypted font programs exported; no PostScript execution or glyph decryption.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_FONT_TYPE1_PFB_H
#define XX_FONT_TYPE1_PFB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_font_type1_pfb {Abstractformat format;} xx_font_type1_pfb;
XXFC_API void xx_font_type1_pfb_init(xx_font_type1_pfb *,xx_io_device *,int64_t);
XXFC_API xx_font_type1_pfb *xx_font_type1_pfb_create(xx_io_device *,int64_t);
XXFC_API void xx_font_type1_pfb_destroy(xx_font_type1_pfb *);
XXFC_API void xx_font_type1_pfb_free(xx_font_type1_pfb *);
XXFC_API bool xx_font_type1_pfb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_font_type1_pfb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_font_type1_pfb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_font_type1_pfb_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_font_type1_pfb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_font_type1_pfb_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_font_type1_pfb_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
