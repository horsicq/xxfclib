/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_MICROSOFT_MSF_H
#define XX_MICROSOFT_MSF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_microsoft_msf { Abstractformat format; } xx_microsoft_msf;
XXFC_API void xx_microsoft_msf_init(xx_microsoft_msf *,xx_io_device *,int64_t);
XXFC_API xx_microsoft_msf *xx_microsoft_msf_create(xx_io_device *,int64_t);
XXFC_API void xx_microsoft_msf_destroy(xx_microsoft_msf *);
XXFC_API void xx_microsoft_msf_free(xx_microsoft_msf *);
XXFC_API bool xx_microsoft_msf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_microsoft_msf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_microsoft_msf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_microsoft_msf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_microsoft_msf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_microsoft_msf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_microsoft_msf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
