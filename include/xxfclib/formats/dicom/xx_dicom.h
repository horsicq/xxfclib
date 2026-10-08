/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://dicom.nema.org/medical/dicom/current/output/html/part10.html, https://dicom.nema.org/medical/dicom/current/output/html/part05.html
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_DICOM_H
#define XX_DICOM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dicom { Abstractformat format; } xx_dicom;
XXFC_API void xx_dicom_init(xx_dicom *,xx_io_device *,int64_t);
XXFC_API xx_dicom *xx_dicom_create(xx_io_device *,int64_t);
XXFC_API void xx_dicom_destroy(xx_dicom *);
XXFC_API void xx_dicom_free(xx_dicom *);
XXFC_API bool xx_dicom_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dicom_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dicom_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dicom_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dicom_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
