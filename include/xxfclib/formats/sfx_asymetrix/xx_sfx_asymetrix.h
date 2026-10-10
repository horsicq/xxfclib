/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_ASYMETRIX_H
#define XX_SFX_ASYMETRIX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_asymetrix {
    Abstractformat format;
} xx_sfx_asymetrix;
XXFC_API void xx_sfx_asymetrix_init(xx_sfx_asymetrix *, xx_io_device *, int64_t);
XXFC_API xx_sfx_asymetrix *xx_sfx_asymetrix_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_asymetrix_destroy(xx_sfx_asymetrix *);
XXFC_API void xx_sfx_asymetrix_free(xx_sfx_asymetrix *);
XXFC_API bool xx_sfx_asymetrix_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_asymetrix_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_asymetrix_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_asymetrix_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_asymetrix_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_asymetrix_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_asymetrix_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
