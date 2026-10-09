/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_ARJ_H
#define XX_SFX_ARJ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_arj { Abstractformat format; } xx_sfx_arj;
XXFC_API void xx_sfx_arj_init(xx_sfx_arj *,xx_io_device *,int64_t);
XXFC_API xx_sfx_arj *xx_sfx_arj_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_arj_destroy(xx_sfx_arj *);
XXFC_API void xx_sfx_arj_free(xx_sfx_arj *);
XXFC_API bool xx_sfx_arj_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_arj_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_arj_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_arj_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_arj_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_arj_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_arj_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
