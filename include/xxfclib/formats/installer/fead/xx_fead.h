/* Copyright (c) 2026 hors<horsicq@gmail.com>; SPDX-License-Identifier: MIT */
#ifndef XX_FEAD_H
#define XX_FEAD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_fead_create(xx_io_device *, int64_t);
XXFC_API void xx_fead_free(Abstractformat *);
XXFC_API xx_file_type_t xx_fead_detect_device(xx_io_device *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_fead_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_fead_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_fead_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_fead_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_fead_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
