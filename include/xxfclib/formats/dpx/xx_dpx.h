/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://pub.smpte.org/latest/st268-1/st0268-1-2014_stable2015.pdf, https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/dpx.c
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_DPX_H
#define XX_DPX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dpx { Abstractformat format; } xx_dpx;
XXFC_API void xx_dpx_init(xx_dpx *,xx_io_device *,int64_t);
XXFC_API xx_dpx *xx_dpx_create(xx_io_device *,int64_t);
XXFC_API void xx_dpx_destroy(xx_dpx *);
XXFC_API void xx_dpx_free(xx_dpx *);
XXFC_API bool xx_dpx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dpx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dpx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dpx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dpx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dpx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dpx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
