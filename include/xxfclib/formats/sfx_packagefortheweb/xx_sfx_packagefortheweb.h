/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/installers/xpftw.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_SFX_PACKAGEFORTHEWEB_H
#define XX_SFX_PACKAGEFORTHEWEB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_packagefortheweb { Abstractformat format; } xx_sfx_packagefortheweb;
XXFC_API void xx_sfx_packagefortheweb_init(xx_sfx_packagefortheweb *,xx_io_device *,int64_t);
XXFC_API xx_sfx_packagefortheweb *xx_sfx_packagefortheweb_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_packagefortheweb_destroy(xx_sfx_packagefortheweb *);
XXFC_API void xx_sfx_packagefortheweb_free(xx_sfx_packagefortheweb *);
XXFC_API bool xx_sfx_packagefortheweb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_packagefortheweb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_packagefortheweb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_packagefortheweb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_packagefortheweb_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
