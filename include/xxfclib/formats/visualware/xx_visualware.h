/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * VisualWare installer.
 */
#ifndef XXFCLIB_FORMAT_VISUALWARE_H
#define XXFCLIB_FORMAT_VISUALWARE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_visualware_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_visualware_free(Abstractformat *format);
XXFC_API xx_file_type_t xx_visualware_detect(xx_io_device *device, int64_t base_address);
XXFC_API bool xx_visualware_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_visualware_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_visualware_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_visualware_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_visualware_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
