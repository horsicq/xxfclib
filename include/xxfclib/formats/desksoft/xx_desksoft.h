/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * DeskSoft installer.
 */
#ifndef XXFCLIB_FORMAT_DESKSOFT_H
#define XXFCLIB_FORMAT_DESKSOFT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_desksoft_create(xx_io_device *device,int64_t base_address);
XXFC_API void xx_desksoft_free(Abstractformat *format);
XXFC_API xx_file_type_t xx_desksoft_detect(xx_io_device *device,int64_t base_address);
XXFC_API bool xx_desksoft_fast_detect(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API xx_file_type_t xx_desksoft_file_type(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API int64_t xx_desksoft_size(xx_io_device *device,int64_t base_address,bool is_mapped);
XXFC_API Abstractextractor *xx_desksoft_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_desksoft_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
