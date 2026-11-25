/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_INSTALLSHIELD_OBL_H
#define XXFCLIB_FORMAT_INSTALLSHIELD_OBL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** InstallShield pOdA libraries; stored OBS members, not script decompilation. */
XXFC_API Abstractformat *xx_installshield_obl_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_installshield_obl_free(Abstractformat *format);
XXFC_API bool xx_installshield_obl_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_installshield_obl_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_installshield_obl_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_installshield_obl_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_installshield_obl_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
