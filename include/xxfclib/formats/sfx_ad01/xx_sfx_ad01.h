/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Active Delivery AD01 PE carrier with a bounded encrypted ZIP payload.
 */
#ifndef XX_SFX_AD01_H
#define XX_SFX_AD01_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_ad01 {
    Abstractformat format;
} xx_sfx_ad01;
XXFC_API void xx_sfx_ad01_init(xx_sfx_ad01 *, xx_io_device *, int64_t);
XXFC_API xx_sfx_ad01 *xx_sfx_ad01_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_ad01_destroy(xx_sfx_ad01 *);
XXFC_API void xx_sfx_ad01_free(xx_sfx_ad01 *);
XXFC_API bool xx_sfx_ad01_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_ad01_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_ad01_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_ad01_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_ad01_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_ad01_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_ad01_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
