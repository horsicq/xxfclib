/* SPDX-License-Identifier: MIT
 * Primary reference: https://adobe-type-tools.github.io/font-tech-notes/pdfs/5004.AFM_Spec.pdf
 * Adobe AFM3/4.1 complete bounded font/global metrics, character metric and optional kerning/track/composite sections. Finite metrics and local glyph references validated; original metrics exported; AMFM/MM and font execution unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_FONT_AFM_H
#define XX_FONT_AFM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_font_afm {Abstractformat format;} xx_font_afm;
XXFC_API void xx_font_afm_init(xx_font_afm *,xx_io_device *,int64_t);
XXFC_API xx_font_afm *xx_font_afm_create(xx_io_device *,int64_t);
XXFC_API void xx_font_afm_destroy(xx_font_afm *);
XXFC_API void xx_font_afm_free(xx_font_afm *);
XXFC_API bool xx_font_afm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_font_afm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_font_afm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_font_afm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_font_afm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_font_afm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_font_afm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
