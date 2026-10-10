/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/JKAnderson/SoulsFormats/blob/master/SoulsFormats/Binder/BND3/BND3.cs
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_FROMSOFTWARE_BINDER_H
#define XX_FROMSOFTWARE_BINDER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_fromsoftware_binder {
    Abstractformat format;
} xx_fromsoftware_binder;
XXFC_API void xx_fromsoftware_binder_init(xx_fromsoftware_binder *, xx_io_device *, int64_t);
XXFC_API xx_fromsoftware_binder *xx_fromsoftware_binder_create(xx_io_device *, int64_t);
XXFC_API void xx_fromsoftware_binder_destroy(xx_fromsoftware_binder *);
XXFC_API void xx_fromsoftware_binder_free(xx_fromsoftware_binder *);
XXFC_API bool xx_fromsoftware_binder_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_fromsoftware_binder_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_fromsoftware_binder_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_fromsoftware_binder_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_fromsoftware_binder_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_fromsoftware_binder_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_fromsoftware_binder_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
