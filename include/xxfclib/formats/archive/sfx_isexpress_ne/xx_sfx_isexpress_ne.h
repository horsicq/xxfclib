/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * InstallShield Express one-file self-extractor (16-bit NE stub). */
#ifndef XX_FORMAT_SFX_ISEXPRESS_NE_H
#define XX_FORMAT_SFX_ISEXPRESS_NE_H
#include "xxfclib/formats/xx_legacy_archive.h"
XX_LEGACY_ARCHIVE_DECLARE(sfx_isexpress_ne);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_isexpress_ne_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_isexpress_ne_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_isexpress_ne_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_isexpress_ne_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_isexpress_ne_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
