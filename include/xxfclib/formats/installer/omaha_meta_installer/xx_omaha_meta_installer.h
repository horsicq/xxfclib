/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Google Update (Omaha) meta-installer: the product installer travels as an
 * "EXE" resource beside the "GOOGLEUPDATE" tag and the "XML" payload manifest.
 */
#ifndef XX_OMAHA_META_INSTALLER_H
#define XX_OMAHA_META_INSTALLER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_omaha_meta_installer {
    Abstractformat format;
} xx_omaha_meta_installer;
XXFC_API void xx_omaha_meta_installer_init(xx_omaha_meta_installer *, xx_io_device *, int64_t);
XXFC_API xx_omaha_meta_installer *xx_omaha_meta_installer_create(xx_io_device *, int64_t);
XXFC_API void xx_omaha_meta_installer_destroy(xx_omaha_meta_installer *);
XXFC_API void xx_omaha_meta_installer_free(xx_omaha_meta_installer *);
XXFC_API bool xx_omaha_meta_installer_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_omaha_meta_installer_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_omaha_meta_installer_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_omaha_meta_installer_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_omaha_meta_installer_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_omaha_meta_installer_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_omaha_meta_installer_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
