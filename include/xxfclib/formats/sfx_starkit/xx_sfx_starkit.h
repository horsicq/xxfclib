/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: src/formats/starkit/xx_starkit.c
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_STARKIT_H
#define XX_SFX_STARKIT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_starkit {
    Abstractformat format;
} xx_sfx_starkit;
XXFC_API void xx_sfx_starkit_init(xx_sfx_starkit *, xx_io_device *, int64_t);
XXFC_API xx_sfx_starkit *xx_sfx_starkit_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_starkit_destroy(xx_sfx_starkit *);
XXFC_API void xx_sfx_starkit_free(xx_sfx_starkit *);
XXFC_API bool xx_sfx_starkit_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_starkit_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_starkit_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_starkit_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_starkit_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_starkit_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_starkit_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
