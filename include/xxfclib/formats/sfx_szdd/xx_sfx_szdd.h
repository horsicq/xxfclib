/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xszddsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_SZDD_H
#define XX_SFX_SZDD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_szdd { Abstractformat format; } xx_sfx_szdd;
XXFC_API void xx_sfx_szdd_init(xx_sfx_szdd *,xx_io_device *,int64_t);
XXFC_API xx_sfx_szdd *xx_sfx_szdd_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_szdd_destroy(xx_sfx_szdd *);
XXFC_API void xx_sfx_szdd_free(xx_sfx_szdd *);
XXFC_API bool xx_sfx_szdd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_szdd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_szdd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_szdd_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_szdd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_szdd_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_szdd_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
