/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_OPENSSH_CERTIFICATE_H
#define XX_OPENSSH_CERTIFICATE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_openssh_certificate {Abstractformat format;} xx_openssh_certificate;
XXFC_API void xx_openssh_certificate_init(xx_openssh_certificate *,xx_io_device *,int64_t);
XXFC_API xx_openssh_certificate *xx_openssh_certificate_create(xx_io_device *,int64_t);
XXFC_API void xx_openssh_certificate_destroy(xx_openssh_certificate *);
XXFC_API void xx_openssh_certificate_free(xx_openssh_certificate *);
XXFC_API bool xx_openssh_certificate_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_openssh_certificate_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_openssh_certificate_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_openssh_certificate_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_openssh_certificate_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_openssh_certificate_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_openssh_certificate_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
