/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * ASD compressed archive.
 */
#ifndef XXFCLIB_FORMAT_ASD_H
#define XXFCLIB_FORMAT_ASD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_asd_create(xx_io_device *device,int64_t base_address);
XXFC_API void xx_asd_free(Abstractformat *format);
XXFC_API xx_file_type_t xx_asd_detect(xx_io_device *device,int64_t base_address);
XXFC_API bool xx_asd_fast_detect(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API xx_file_type_t xx_asd_file_type(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API int64_t xx_asd_size(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API Abstractextractor *xx_asd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_asd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
