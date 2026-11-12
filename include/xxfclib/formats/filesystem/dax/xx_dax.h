/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * DAX: the family header of the dax type, served by the shared reader
 * declared in filesystem/ciso/xx_ciso.h.
 */
#ifndef XX_DAX_FAMILY_H
#define XX_DAX_FAMILY_H
#include "xxfclib/formats/filesystem/ciso/xx_ciso.h"
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dax_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dax_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dax_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dax_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dax_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XX_DAX_FAMILY_H */
