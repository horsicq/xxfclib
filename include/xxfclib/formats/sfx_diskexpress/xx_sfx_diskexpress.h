/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_DISKEXPRESS_H
#define XX_SFX_DISKEXPRESS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_diskexpress {
    Abstractformat format;
} xx_sfx_diskexpress;
XXFC_API void xx_sfx_diskexpress_init(xx_sfx_diskexpress *, xx_io_device *, int64_t);
XXFC_API xx_sfx_diskexpress *xx_sfx_diskexpress_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_diskexpress_destroy(xx_sfx_diskexpress *);
XXFC_API void xx_sfx_diskexpress_free(xx_sfx_diskexpress *);
XXFC_API bool xx_sfx_diskexpress_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_diskexpress_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_diskexpress_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_diskexpress_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_diskexpress_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_diskexpress_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_diskexpress_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
