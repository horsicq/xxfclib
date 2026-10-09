/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Perfare/AssetStudio/master/AssetStudio/BundleFile.cs
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_UNITYFS_H
#define XX_UNITYFS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_unityfs { Abstractformat format; } xx_unityfs;
XXFC_API void xx_unityfs_init(xx_unityfs *,xx_io_device *,int64_t);
XXFC_API xx_unityfs *xx_unityfs_create(xx_io_device *,int64_t);
XXFC_API void xx_unityfs_destroy(xx_unityfs *);
XXFC_API void xx_unityfs_free(xx_unityfs *);
XXFC_API bool xx_unityfs_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_unityfs_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_unityfs_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_unityfs_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_unityfs_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_unityfs_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_unityfs_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
