/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/xm_load.c, https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/xm.h
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_TRACKER_XM_H
#define XX_TRACKER_XM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_xm { Abstractformat format; } xx_tracker_xm;
XXFC_API void xx_tracker_xm_init(xx_tracker_xm *,xx_io_device *,int64_t);
XXFC_API xx_tracker_xm *xx_tracker_xm_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_xm_destroy(xx_tracker_xm *);
XXFC_API void xx_tracker_xm_free(xx_tracker_xm *);
XXFC_API bool xx_tracker_xm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_xm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_xm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tracker_xm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_xm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_xm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tracker_xm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
