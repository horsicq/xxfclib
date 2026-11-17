/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CreateInstall standalone package (DISK1.PAK, LZARI members). */
#ifndef XX_FORMAT_CREATEINSTALL_PAK_H
#define XX_FORMAT_CREATEINSTALL_PAK_H
#include "xxfclib/formats/xx_legacy_archive.h"
XX_LEGACY_ARCHIVE_DECLARE(createinstall_pak);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_createinstall_pak_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_createinstall_pak_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_createinstall_pak_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_createinstall_pak_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_createinstall_pak_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
