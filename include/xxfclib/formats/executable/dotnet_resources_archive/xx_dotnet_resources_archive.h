/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * .NET assemblies that embed files as manifest resources: assemblies merged
 * in (Costura, hand-rolled loaders), licence texts, data files, resource sets.
 */
#ifndef XX_DOTNET_RESOURCES_ARCHIVE_H
#define XX_DOTNET_RESOURCES_ARCHIVE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dotnet_resources_archive {
    Abstractformat format;
} xx_dotnet_resources_archive;
XXFC_API void xx_dotnet_resources_archive_init(xx_dotnet_resources_archive *, xx_io_device *, int64_t);
XXFC_API xx_dotnet_resources_archive *xx_dotnet_resources_archive_create(xx_io_device *, int64_t);
XXFC_API void xx_dotnet_resources_archive_destroy(xx_dotnet_resources_archive *);
XXFC_API void xx_dotnet_resources_archive_free(xx_dotnet_resources_archive *);
XXFC_API bool xx_dotnet_resources_archive_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dotnet_resources_archive_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dotnet_resources_archive_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dotnet_resources_archive_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dotnet_resources_archive_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dotnet_resources_archive_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dotnet_resources_archive_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
