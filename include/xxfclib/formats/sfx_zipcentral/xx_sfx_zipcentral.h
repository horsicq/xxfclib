/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_ZIPCENTRAL_H
#define XX_SFX_ZIPCENTRAL_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/zip/xx_zip.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_zipcentral {
    Abstractformat format;
    xx_zip inner;
    void *relative_view;
    wchar_t *legacy_name;
    int64_t legacy_local_offset;
    bool inner_ready;
    bool checked_absolute;
    bool checked_relative;
} xx_sfx_zipcentral;
XXFC_API void xx_sfx_zipcentral_init(xx_sfx_zipcentral *,xx_io_device *,int64_t);
XXFC_API xx_sfx_zipcentral *xx_sfx_zipcentral_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_zipcentral_destroy(xx_sfx_zipcentral *);
XXFC_API void xx_sfx_zipcentral_free(xx_sfx_zipcentral *);
XXFC_API bool xx_sfx_zipcentral_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_zipcentral_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_zipcentral_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_zipcentral_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_zipcentral_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_zipcentral_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_zipcentral_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
