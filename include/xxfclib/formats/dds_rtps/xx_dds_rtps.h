/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_DDS_RTPS_H
#define XX_DDS_RTPS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dds_rtps {
    Abstractformat format;
} xx_dds_rtps;
XXFC_API void xx_dds_rtps_init(xx_dds_rtps *, xx_io_device *, int64_t);
XXFC_API xx_dds_rtps *xx_dds_rtps_create(xx_io_device *, int64_t);
XXFC_API void xx_dds_rtps_destroy(xx_dds_rtps *);
XXFC_API void xx_dds_rtps_free(xx_dds_rtps *);
XXFC_API bool xx_dds_rtps_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dds_rtps_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dds_rtps_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dds_rtps_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dds_rtps_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dds_rtps_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dds_rtps_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
