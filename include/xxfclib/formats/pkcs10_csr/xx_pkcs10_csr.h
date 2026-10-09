/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_PKCS10_CSR_H
#define XX_PKCS10_CSR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pkcs10_csr { Abstractformat format; } xx_pkcs10_csr;
XXFC_API void xx_pkcs10_csr_init(xx_pkcs10_csr *,xx_io_device *,int64_t);
XXFC_API xx_pkcs10_csr *xx_pkcs10_csr_create(xx_io_device *,int64_t);
XXFC_API void xx_pkcs10_csr_destroy(xx_pkcs10_csr *);
XXFC_API void xx_pkcs10_csr_free(xx_pkcs10_csr *);
XXFC_API bool xx_pkcs10_csr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pkcs10_csr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pkcs10_csr_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pkcs10_csr_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pkcs10_csr_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pkcs10_csr_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pkcs10_csr_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
