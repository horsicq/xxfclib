/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * OS/2 RSFX carrier with a bounded RAR 1.5 member stream.
 */
#ifndef XX_SFX_RSFX_H
#define XX_SFX_RSFX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
struct xx_rar;
typedef struct xx_sfx_rsfx {
    Abstractformat format;
    struct xx_rar *inner;
    xx_io_device *normalized_device;
    uint8_t *normalized_data;
} xx_sfx_rsfx;
XXFC_API void xx_sfx_rsfx_init(xx_sfx_rsfx *, xx_io_device *, int64_t);
XXFC_API xx_sfx_rsfx *xx_sfx_rsfx_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_rsfx_destroy(xx_sfx_rsfx *);
XXFC_API void xx_sfx_rsfx_free(xx_sfx_rsfx *);
XXFC_API bool xx_sfx_rsfx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_rsfx_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_rsfx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_rsfx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_rsfx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_rsfx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_rsfx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
