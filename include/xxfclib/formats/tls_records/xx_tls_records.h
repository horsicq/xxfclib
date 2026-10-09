/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_TLS_RECORDS_H
#define XX_TLS_RECORDS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tls_records {Abstractformat format;} xx_tls_records;
XXFC_API void xx_tls_records_init(xx_tls_records *,xx_io_device *,int64_t);
XXFC_API xx_tls_records *xx_tls_records_create(xx_io_device *,int64_t);
XXFC_API void xx_tls_records_destroy(xx_tls_records *);
XXFC_API void xx_tls_records_free(xx_tls_records *);
XXFC_API bool xx_tls_records_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tls_records_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tls_records_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tls_records_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tls_records_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tls_records_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tls_records_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
