/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. Standalone CLI metadata root 1.1, including portable PDB streams; exports streams without decoding rows or executing code.
 */
#ifndef XX_DOTNET_METADATA_H
#define XX_DOTNET_METADATA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dotnet_metadata { Abstractformat format; } xx_dotnet_metadata;
XXFC_API void xx_dotnet_metadata_init(xx_dotnet_metadata *,xx_io_device *,int64_t);
XXFC_API xx_dotnet_metadata *xx_dotnet_metadata_create(xx_io_device *,int64_t);
XXFC_API void xx_dotnet_metadata_destroy(xx_dotnet_metadata *);
XXFC_API void xx_dotnet_metadata_free(xx_dotnet_metadata *);
XXFC_API bool xx_dotnet_metadata_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dotnet_metadata_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dotnet_metadata_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dotnet_metadata_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dotnet_metadata_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
