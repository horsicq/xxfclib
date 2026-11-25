/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_STOR_RESOURCE_H
#define XXFCLIB_FORMAT_STOR_RESOURCE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Stored STOR resources: eight-byte header and fixed 21-byte directory.
 * The validated directory ends at EOF or a complete following Smacker extent.
 * Trailing data is exposed as overlay metadata, not as indexed members. */
XXFC_API Abstractformat *xx_stor_resource_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_stor_resource_free(Abstractformat *format);
XXFC_API bool xx_stor_resource_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_stor_resource_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_stor_resource_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_stor_resource_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_stor_resource_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
