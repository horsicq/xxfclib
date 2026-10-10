/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_CAB_H
#define XX_SFX_CAB_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/cab/xx_cab.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_cab {
    Abstractformat format;
    xx_cab inner;
    int64_t payload_offset;
    bool inner_ready;
    bool resource_member;
} xx_sfx_cab;
XXFC_API void xx_sfx_cab_init(xx_sfx_cab *, xx_io_device *, int64_t);
XXFC_API xx_sfx_cab *xx_sfx_cab_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_cab_destroy(xx_sfx_cab *);
XXFC_API void xx_sfx_cab_free(xx_sfx_cab *);
XXFC_API bool xx_sfx_cab_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_cab_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_cab_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sfx_cab_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_cab_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_cab_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sfx_cab_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
