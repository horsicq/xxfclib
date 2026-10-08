/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://android.googlesource.com/platform/frameworks/base/+/13ac041/services/java/com/android/server/BackupManagerService.java
 * Publishes stored payload components; see docs/registered_second_fifty_formats.md.
 */
#ifndef XX_ANDROID_AB_H
#define XX_ANDROID_AB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_android_ab { Abstractformat format; } xx_android_ab;
XXFC_API void xx_android_ab_init(xx_android_ab *,xx_io_device *,int64_t);
XXFC_API xx_android_ab *xx_android_ab_create(xx_io_device *,int64_t);
XXFC_API void xx_android_ab_destroy(xx_android_ab *);
XXFC_API void xx_android_ab_free(xx_android_ab *);
XXFC_API bool xx_android_ab_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_android_ab_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_android_ab_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_android_ab_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_android_ab_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
