/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/installers/xinstallanywhere.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_INSTALLANYWHERE_UNIX_H
#define XX_INSTALLANYWHERE_UNIX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_installanywhere_unix { Abstractformat format; } xx_installanywhere_unix;
XXFC_API void xx_installanywhere_unix_init(xx_installanywhere_unix *,xx_io_device *,int64_t);
XXFC_API xx_installanywhere_unix *xx_installanywhere_unix_create(xx_io_device *,int64_t);
XXFC_API void xx_installanywhere_unix_destroy(xx_installanywhere_unix *);
XXFC_API void xx_installanywhere_unix_free(xx_installanywhere_unix *);
XXFC_API bool xx_installanywhere_unix_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_installanywhere_unix_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_installanywhere_unix_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_installanywhere_unix_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_installanywhere_unix_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_installanywhere_unix_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_installanywhere_unix_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
