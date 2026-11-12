/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_CARMAN_H
#define XXFCLIB_FORMAT_CARMAN_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* CARMAN archives (Mark Nelson, "The Data Compression Book"): NUL-terminated
 * member names, a method byte, sizes and two CRC-32s per member. */
XXFC_API Abstractformat *xx_carman_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_carman_free(Abstractformat *format);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_carman_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_carman_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_carman_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_carman_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_carman_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
