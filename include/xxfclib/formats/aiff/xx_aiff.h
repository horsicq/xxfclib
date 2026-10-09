/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/Docs/AIFF-1.3.pdf, https://raw.githubusercontent.com/libsndfile/libsndfile/master/src/aiff.c
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_AIFF_H
#define XX_AIFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_aiff { Abstractformat format; } xx_aiff;
XXFC_API void xx_aiff_init(xx_aiff *,xx_io_device *,int64_t);
XXFC_API xx_aiff *xx_aiff_create(xx_io_device *,int64_t);
XXFC_API void xx_aiff_destroy(xx_aiff *);
XXFC_API void xx_aiff_free(xx_aiff *);
XXFC_API bool xx_aiff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_aiff_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_aiff_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_aiff_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_aiff_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_aiff_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_aiff_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
