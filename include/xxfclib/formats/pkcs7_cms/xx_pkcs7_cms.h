/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_PKCS7_CMS_H
#define XX_PKCS7_CMS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pkcs7_cms { Abstractformat format; } xx_pkcs7_cms;
XXFC_API void xx_pkcs7_cms_init(xx_pkcs7_cms *,xx_io_device *,int64_t);
XXFC_API xx_pkcs7_cms *xx_pkcs7_cms_create(xx_io_device *,int64_t);
XXFC_API void xx_pkcs7_cms_destroy(xx_pkcs7_cms *);
XXFC_API void xx_pkcs7_cms_free(xx_pkcs7_cms *);
XXFC_API bool xx_pkcs7_cms_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pkcs7_cms_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
