/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://fontforge.org/docs/techref/pcf-format.html
 * X11 PCF directory and complete typed properties, accelerators, metrics, bitmap, encoding, width and glyph-name tables, including the original bdftopcf final
 * accelerator100/72-byte convention. Counts, names, bitmap sizes and character references are bounded. Original typed tables remain encoded; unknown table types/format
 * flags and font rendering are unsupported. Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_FONT_PCF_H
#define XX_FONT_PCF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_font_pcf {
    Abstractformat format;
} xx_font_pcf;
XXFC_API void xx_font_pcf_init(xx_font_pcf *, xx_io_device *, int64_t);
XXFC_API xx_font_pcf *xx_font_pcf_create(xx_io_device *, int64_t);
XXFC_API void xx_font_pcf_destroy(xx_font_pcf *);
XXFC_API void xx_font_pcf_free(xx_font_pcf *);
XXFC_API bool xx_font_pcf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_font_pcf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_font_pcf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_font_pcf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_font_pcf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_font_pcf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_font_pcf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
