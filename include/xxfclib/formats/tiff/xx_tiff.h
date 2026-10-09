/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://raw.githubusercontent.com/libsdl-org/libtiff/master/libtiff/tif_dirread.c, https://libtiff.gitlab.io/libtiff/specification/bigtiff.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_TIFF_H
#define XX_TIFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tiff { Abstractformat format; } xx_tiff;
XXFC_API void xx_tiff_init(xx_tiff *,xx_io_device *,int64_t);
XXFC_API xx_tiff *xx_tiff_create(xx_io_device *,int64_t);
XXFC_API void xx_tiff_destroy(xx_tiff *);
XXFC_API void xx_tiff_free(xx_tiff *);
XXFC_API bool xx_tiff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tiff_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tiff_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tiff_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tiff_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tiff_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tiff_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
