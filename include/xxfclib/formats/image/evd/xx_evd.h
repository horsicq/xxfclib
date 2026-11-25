/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * EVD map images.
 */
#ifndef XXFCLIB_FORMAT_EVD_H
#define XXFCLIB_FORMAT_EVD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_evd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_evd_free(Abstractformat *format);
XXFC_API xx_file_type_t xx_evd_detect(xx_io_device *device, int64_t base_address);
XXFC_API bool xx_evd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_evd_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_evd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_evd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_evd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
