/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Commodore 64 self-dissolving archive (BASIC SYS stub + CBM ARC records). */
#ifndef XX_FORMAT_C64_SDA_H
#define XX_FORMAT_C64_SDA_H
#include "xxfclib/formats/xx_legacy_archive.h"
XX_LEGACY_ARCHIVE_DECLARE(c64_sda);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_c64_sda_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_c64_sda_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_c64_sda_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_c64_sda_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_c64_sda_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
