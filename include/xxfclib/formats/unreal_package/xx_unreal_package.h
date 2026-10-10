/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/gildor2/UEViewer/master/Unreal/UnrealPackage/UnPackage2.cpp
 * Unreal Engine1 versions61-63, licensee0, little endian, uncompressed export objects. Validates compact indices, name/import/export/heritage tables and object ranges.
 * Later UE versions, game-specific encryption/compression and object decoding/execution unsupported.
 */
#ifndef XX_UNREAL_PACKAGE_H
#define XX_UNREAL_PACKAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_unreal_package {
    Abstractformat format;
} xx_unreal_package;
XXFC_API void xx_unreal_package_init(xx_unreal_package *, xx_io_device *, int64_t);
XXFC_API xx_unreal_package *xx_unreal_package_create(xx_io_device *, int64_t);
XXFC_API void xx_unreal_package_destroy(xx_unreal_package *);
XXFC_API void xx_unreal_package_free(xx_unreal_package *);
XXFC_API bool xx_unreal_package_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_unreal_package_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_unreal_package_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_unreal_package_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_unreal_package_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_unreal_package_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_unreal_package_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
