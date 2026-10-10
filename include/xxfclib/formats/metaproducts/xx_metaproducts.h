/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * MetaProducts installer.
 */
#ifndef XXFCLIB_FORMAT_METAPRODUCTS_H
#define XXFCLIB_FORMAT_METAPRODUCTS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_metaproducts_create(xx_io_device *device,int64_t base_address);
XXFC_API void xx_metaproducts_free(Abstractformat *format);
XXFC_API xx_file_type_t xx_metaproducts_detect(xx_io_device *device,int64_t base_address);
XXFC_API bool xx_metaproducts_fast_detect(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API xx_file_type_t xx_metaproducts_file_type(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API int64_t xx_metaproducts_size(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API Abstractextractor *xx_metaproducts_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_metaproducts_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
