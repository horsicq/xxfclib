/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xwinimageziparchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_WINIMAGE_ZIP_H
#define XX_WINIMAGE_ZIP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_winimage_zip { Abstractformat format; } xx_winimage_zip;
XXFC_API void xx_winimage_zip_init(xx_winimage_zip *,xx_io_device *,int64_t);
XXFC_API xx_winimage_zip *xx_winimage_zip_create(xx_io_device *,int64_t);
XXFC_API void xx_winimage_zip_destroy(xx_winimage_zip *);
XXFC_API void xx_winimage_zip_free(xx_winimage_zip *);
XXFC_API bool xx_winimage_zip_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_winimage_zip_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_winimage_zip_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_winimage_zip_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_winimage_zip_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
