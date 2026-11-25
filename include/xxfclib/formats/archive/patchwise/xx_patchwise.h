/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_PATCHWISE_H
#define XXFCLIB_FORMAT_PATCHWISE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** PatchWise ZSB bzip2 chunks and stored logical members. */
XXFC_API Abstractformat *xx_patchwise_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_patchwise_free(Abstractformat *format);
XXFC_API bool xx_patchwise_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_patchwise_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_patchwise_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_patchwise_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_patchwise_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
