/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Sony TCJN image.
 */
#ifndef XXFCLIB_FORMAT_SONY_IMAGE_H
#define XXFCLIB_FORMAT_SONY_IMAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_sony_image_create(xx_io_device *device,int64_t base_address);
XXFC_API void xx_sony_image_free(Abstractformat *format);
XXFC_API xx_file_type_t xx_sony_image_detect(xx_io_device *device,int64_t base_address);
XXFC_API bool xx_sony_image_fast_detect(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API xx_file_type_t xx_sony_image_file_type(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API int64_t xx_sony_image_size(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API Abstractextractor *xx_sony_image_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sony_image_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
