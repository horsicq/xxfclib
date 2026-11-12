/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * .NET single-file application bundle: an apphost executable with the
 * application's assemblies and configuration appended, described by a
 * manifest the host finds through a marker in its own image.
 */
#ifndef XX_DOTNET_BUNDLE_H
#define XX_DOTNET_BUNDLE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dotnet_bundle {
    Abstractformat format;
} xx_dotnet_bundle;
XXFC_API void xx_dotnet_bundle_init(xx_dotnet_bundle *, xx_io_device *, int64_t);
XXFC_API xx_dotnet_bundle *xx_dotnet_bundle_create(xx_io_device *, int64_t);
XXFC_API void xx_dotnet_bundle_destroy(xx_dotnet_bundle *);
XXFC_API void xx_dotnet_bundle_free(xx_dotnet_bundle *);
XXFC_API bool xx_dotnet_bundle_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dotnet_bundle_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dotnet_bundle_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dotnet_bundle_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dotnet_bundle_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dotnet_bundle_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dotnet_bundle_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
