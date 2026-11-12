/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Windows Installer packages carried as resources of a setup stub: stored
 * compound files (RCDATA "MSI") or XOR-0xFF inverted ones (the "MSI_FILE"
 * resources of the Classic Shell / Open-Shell setup).
 */
#ifndef XX_MSI_RESOURCE_H
#define XX_MSI_RESOURCE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_msi_resource {
    Abstractformat format;
} xx_msi_resource;
XXFC_API void xx_msi_resource_init(xx_msi_resource *, xx_io_device *, int64_t);
XXFC_API xx_msi_resource *xx_msi_resource_create(xx_io_device *, int64_t);
XXFC_API void xx_msi_resource_destroy(xx_msi_resource *);
XXFC_API void xx_msi_resource_free(xx_msi_resource *);
XXFC_API bool xx_msi_resource_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_msi_resource_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_msi_resource_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_msi_resource_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_msi_resource_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_msi_resource_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_msi_resource_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
