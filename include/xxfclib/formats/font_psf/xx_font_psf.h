/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/legionus/kbd/master/src/libkfont/psffontop.c
 * PSF1/PSF2 console bitmap fonts with exact glyph geometry, complete bitmap array and optional fully terminated scalar Unicode mapping/sequence grammar. Encoded bitmap and mappings are exported; font rendering and Sony PSF audio semantics are unrelated.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_FONT_PSF_H
#define XX_FONT_PSF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_font_psf { Abstractformat format; } xx_font_psf;
XXFC_API void xx_font_psf_init(xx_font_psf *,xx_io_device *,int64_t);
XXFC_API xx_font_psf *xx_font_psf_create(xx_io_device *,int64_t);
XXFC_API void xx_font_psf_destroy(xx_font_psf *);
XXFC_API void xx_font_psf_free(xx_font_psf *);
XXFC_API bool xx_font_psf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_font_psf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_font_psf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_font_psf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_font_psf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
