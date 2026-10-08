/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xtgcfarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_TGCF_H
#define XX_SFX_TGCF_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/tgcf/xx_tgcf.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_tgcf {
    Abstractformat format;
    xx_tgcf inner;
    bool inner_ready;
    bool checked_inner;
} xx_sfx_tgcf;
XXFC_API void xx_sfx_tgcf_init(xx_sfx_tgcf *,xx_io_device *,int64_t);
XXFC_API xx_sfx_tgcf *xx_sfx_tgcf_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_tgcf_destroy(xx_sfx_tgcf *);
XXFC_API void xx_sfx_tgcf_free(xx_sfx_tgcf *);
XXFC_API bool xx_sfx_tgcf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_tgcf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_tgcf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_tgcf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_tgcf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
