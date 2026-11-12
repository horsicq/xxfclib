/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * DGCA: the family header of the DGCA reader, served by the legacy archive engine
 * declared in archive/legacy_archive_engine/xx_legacy_archive_engine.h.
 */
#ifndef XX_DGCA_FAMILY_H
#define XX_DGCA_FAMILY_H
#include "xxfclib/formats/archive/legacy_archive_engine/xx_legacy_archive_engine.h"
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dgca_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dgca_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dgca_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dgca_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dgca_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XX_DGCA_FAMILY_H */
