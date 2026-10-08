/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Headers/IconsCore.h
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_ICNS_H
#define XX_ICNS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_icns { Abstractformat format; } xx_icns;
XXFC_API void xx_icns_init(xx_icns *,xx_io_device *,int64_t);
XXFC_API xx_icns *xx_icns_create(xx_io_device *,int64_t);
XXFC_API void xx_icns_destroy(xx_icns *);
XXFC_API void xx_icns_free(xx_icns *);
XXFC_API bool xx_icns_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_icns_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_icns_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_icns_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_icns_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
