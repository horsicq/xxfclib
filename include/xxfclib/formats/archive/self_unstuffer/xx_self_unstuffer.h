/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_SELF_UNSTUFFER_H
#define XXFCLIB_FORMAT_SELF_UNSTUFFER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Aladdin DOS Self-UnStuffer stored payload index. */
XXFC_API Abstractformat *xx_self_unstuffer_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_self_unstuffer_free(Abstractformat *format);
XXFC_API bool xx_self_unstuffer_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_self_unstuffer_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_self_unstuffer_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_self_unstuffer_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_self_unstuffer_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
