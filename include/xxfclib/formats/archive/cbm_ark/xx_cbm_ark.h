/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_CBM_ARK_H
#define XXFCLIB_FORMAT_CBM_ARK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Commodore 64 ARK archives: a PETSCII directory of 29-byte entries and
 * the files stored in 254-byte blocks. */
XXFC_API Abstractformat *xx_cbm_ark_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cbm_ark_free(Abstractformat *format);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cbm_ark_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cbm_ark_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cbm_ark_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cbm_ark_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cbm_ark_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
