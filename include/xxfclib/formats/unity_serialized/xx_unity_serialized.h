/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Perfare/AssetStudio/master/AssetStudio/SerializedFile.cs
 * Unity SerializedFile version22 extended headers, little-endian metadata, disabled type trees, up to64 nonscript classes and1024 stored objects. Validates type IDs, unique path IDs, object ranges, metadata end and absent external/script/ref-type tables. Exports object bytes; UnityFS, script objects, type trees, dependency resolution and deserialization unsupported.
 */
#ifndef XX_UNITY_SERIALIZED_H
#define XX_UNITY_SERIALIZED_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_unity_serialized { Abstractformat format; } xx_unity_serialized;
XXFC_API void xx_unity_serialized_init(xx_unity_serialized *,xx_io_device *,int64_t);
XXFC_API xx_unity_serialized *xx_unity_serialized_create(xx_io_device *,int64_t);
XXFC_API void xx_unity_serialized_destroy(xx_unity_serialized *);
XXFC_API void xx_unity_serialized_free(xx_unity_serialized *);
XXFC_API bool xx_unity_serialized_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_unity_serialized_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_unity_serialized_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_unity_serialized_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_unity_serialized_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
