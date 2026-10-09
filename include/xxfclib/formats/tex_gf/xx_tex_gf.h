/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/TeX-Live/texlive-source/trunk/texk/web2c/gftype.web
 * TeX GF131 complete paint/skip/new-row glyph programs, glyph and postamble bounds, character backpointers, specials, postamble/locators and aligned postpost padding. Original encoded font glyphs/metadata exported; no specials execution or raster rendering.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_TEX_GF_H
#define XX_TEX_GF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tex_gf {Abstractformat format;} xx_tex_gf;
XXFC_API void xx_tex_gf_init(xx_tex_gf *,xx_io_device *,int64_t);
XXFC_API xx_tex_gf *xx_tex_gf_create(xx_io_device *,int64_t);
XXFC_API void xx_tex_gf_destroy(xx_tex_gf *);
XXFC_API void xx_tex_gf_free(xx_tex_gf *);
XXFC_API bool xx_tex_gf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tex_gf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tex_gf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tex_gf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tex_gf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tex_gf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tex_gf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
