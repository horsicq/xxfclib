/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_X509_CERTIFICATE_H
#define XX_X509_CERTIFICATE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_x509_certificate { Abstractformat format; } xx_x509_certificate;
XXFC_API void xx_x509_certificate_init(xx_x509_certificate *,xx_io_device *,int64_t);
XXFC_API xx_x509_certificate *xx_x509_certificate_create(xx_io_device *,int64_t);
XXFC_API void xx_x509_certificate_destroy(xx_x509_certificate *);
XXFC_API void xx_x509_certificate_free(xx_x509_certificate *);
XXFC_API bool xx_x509_certificate_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_x509_certificate_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
