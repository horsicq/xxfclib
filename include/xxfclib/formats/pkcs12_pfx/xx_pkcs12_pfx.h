/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_PKCS12_PFX_H
#define XX_PKCS12_PFX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pkcs12_pfx { Abstractformat format; } xx_pkcs12_pfx;
XXFC_API void xx_pkcs12_pfx_init(xx_pkcs12_pfx *,xx_io_device *,int64_t);
XXFC_API xx_pkcs12_pfx *xx_pkcs12_pfx_create(xx_io_device *,int64_t);
XXFC_API void xx_pkcs12_pfx_destroy(xx_pkcs12_pfx *);
XXFC_API void xx_pkcs12_pfx_free(xx_pkcs12_pfx *);
XXFC_API bool xx_pkcs12_pfx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pkcs12_pfx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pkcs12_pfx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pkcs12_pfx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pkcs12_pfx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pkcs12_pfx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pkcs12_pfx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
