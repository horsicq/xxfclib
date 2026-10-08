/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/s3m_load.c, https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/s3m.h
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_TRACKER_S3M_H
#define XX_TRACKER_S3M_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_s3m { Abstractformat format; } xx_tracker_s3m;
XXFC_API void xx_tracker_s3m_init(xx_tracker_s3m *,xx_io_device *,int64_t);
XXFC_API xx_tracker_s3m *xx_tracker_s3m_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_s3m_destroy(xx_tracker_s3m *);
XXFC_API void xx_tracker_s3m_free(xx_tracker_s3m *);
XXFC_API bool xx_tracker_s3m_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_s3m_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_s3m_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_s3m_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_s3m_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
