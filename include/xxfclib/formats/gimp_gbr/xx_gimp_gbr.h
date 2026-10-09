/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://developer.gimp.org/core/standards/gbr/
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_GIMP_GBR_H
#define XX_GIMP_GBR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gimp_gbr { Abstractformat format; } xx_gimp_gbr;
XXFC_API void xx_gimp_gbr_init(xx_gimp_gbr *,xx_io_device *,int64_t);
XXFC_API xx_gimp_gbr *xx_gimp_gbr_create(xx_io_device *,int64_t);
XXFC_API void xx_gimp_gbr_destroy(xx_gimp_gbr *);
XXFC_API void xx_gimp_gbr_free(xx_gimp_gbr *);
XXFC_API bool xx_gimp_gbr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gimp_gbr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gimp_gbr_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_gimp_gbr_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gimp_gbr_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gimp_gbr_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_gimp_gbr_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
