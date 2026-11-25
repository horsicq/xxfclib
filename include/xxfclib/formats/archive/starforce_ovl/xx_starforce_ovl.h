/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_STARFORCE_OVL_H
#define XXFCLIB_FORMAT_STARFORCE_OVL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** StarForce OVL version 2 zlib resource archive. */
XXFC_API Abstractformat *xx_starforce_ovl_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_starforce_ovl_free(Abstractformat *format);
XXFC_API bool xx_starforce_ovl_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_starforce_ovl_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_starforce_ovl_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_starforce_ovl_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_starforce_ovl_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
