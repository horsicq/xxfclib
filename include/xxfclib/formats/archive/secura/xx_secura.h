/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_SECURA_H
#define XXFCLIB_FORMAT_SECURA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Secura SAC1 version 2 stored and LZSS block archive. */
XXFC_API Abstractformat *xx_secura_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_secura_free(Abstractformat *format);
XXFC_API bool xx_secura_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_secura_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_secura_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_secura_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_secura_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
