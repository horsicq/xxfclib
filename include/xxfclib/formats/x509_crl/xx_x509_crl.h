/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_X509_CRL_H
#define XX_X509_CRL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_x509_crl {Abstractformat format;} xx_x509_crl;
XXFC_API void xx_x509_crl_init(xx_x509_crl *,xx_io_device *,int64_t);
XXFC_API xx_x509_crl *xx_x509_crl_create(xx_io_device *,int64_t);
XXFC_API void xx_x509_crl_destroy(xx_x509_crl *);
XXFC_API void xx_x509_crl_free(xx_x509_crl *);
XXFC_API bool xx_x509_crl_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_x509_crl_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_x509_crl_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_x509_crl_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_x509_crl_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
