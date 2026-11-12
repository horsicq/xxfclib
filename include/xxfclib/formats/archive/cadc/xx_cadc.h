/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_CADC_H
#define XXFCLIB_FORMAT_CADC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* CADC .CAR archives (IBM IPF documentation containers): 0x03 0x01 tagged
 * records with a NUL-terminated name, a method byte, sizes, a CRC-32 of the
 * original data and a DOS stamp; stored or flag-byte LZSS members. */
XXFC_API Abstractformat *xx_cadc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cadc_free(Abstractformat *format);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cadc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cadc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cadc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cadc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cadc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
