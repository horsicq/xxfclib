/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * WINDOWS_HELP: the family header of the windows_help type, served by the shared reader
 * declared in document/ue2_documents/xx_ue2_documents.h.
 */
#ifndef XX_WINDOWS_HELP_FAMILY_H
#define XX_WINDOWS_HELP_FAMILY_H
#include "xxfclib/formats/document/ue2_documents/xx_ue2_documents.h"
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_windows_help_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_windows_help_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_windows_help_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_windows_help_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_windows_help_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XX_WINDOWS_HELP_FAMILY_H */
