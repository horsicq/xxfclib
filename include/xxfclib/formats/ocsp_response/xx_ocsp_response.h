/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_OCSP_RESPONSE_H
#define XX_OCSP_RESPONSE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ocsp_response {
    Abstractformat format;
} xx_ocsp_response;
XXFC_API void xx_ocsp_response_init(xx_ocsp_response *, xx_io_device *, int64_t);
XXFC_API xx_ocsp_response *xx_ocsp_response_create(xx_io_device *, int64_t);
XXFC_API void xx_ocsp_response_destroy(xx_ocsp_response *);
XXFC_API void xx_ocsp_response_free(xx_ocsp_response *);
XXFC_API bool xx_ocsp_response_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ocsp_response_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ocsp_response_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ocsp_response_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ocsp_response_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ocsp_response_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ocsp_response_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
