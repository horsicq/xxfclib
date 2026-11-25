/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CopyFloppyDisk compressed image.
 */
#ifndef XXFCLIB_FORMAT_CFD_H
#define XXFCLIB_FORMAT_CFD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API Abstractformat *xx_cfd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cfd_free(Abstractformat *format);
XXFC_API xx_file_type_t xx_cfd_detect(xx_io_device *device, int64_t base_address);
XXFC_API bool xx_cfd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cfd_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cfd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cfd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cfd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
