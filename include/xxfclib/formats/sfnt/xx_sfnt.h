/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. Single SFNT fonts (TrueType/OpenType); validates directory and table checksums, exports tables; no TTC collection or glyph rendering.
 */
#ifndef XX_SFNT_H
#define XX_SFNT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfnt { Abstractformat format; } xx_sfnt;
XXFC_API void xx_sfnt_init(xx_sfnt *,xx_io_device *,int64_t);
XXFC_API xx_sfnt *xx_sfnt_create(xx_io_device *,int64_t);
XXFC_API void xx_sfnt_destroy(xx_sfnt *);
XXFC_API void xx_sfnt_free(xx_sfnt *);
XXFC_API bool xx_sfnt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfnt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfnt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfnt_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfnt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfnt_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfnt_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
