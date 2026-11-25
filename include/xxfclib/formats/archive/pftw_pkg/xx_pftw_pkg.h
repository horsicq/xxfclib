/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_PFTW_PKG_H
#define XXFCLIB_FORMAT_PFTW_PKG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Encoded standalone PackageForTheWeb CAB; borrowed decoded view, native members. */
XXFC_API Abstractformat *xx_pftw_pkg_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pftw_pkg_free(Abstractformat *format);
XXFC_API bool xx_pftw_pkg_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pftw_pkg_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pftw_pkg_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pftw_pkg_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pftw_pkg_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
