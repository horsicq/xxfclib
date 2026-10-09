/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_POSTGRES_CUSTOM_H
#define XX_POSTGRES_CUSTOM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_postgres_custom { Abstractformat format; } xx_postgres_custom;
XXFC_API void xx_postgres_custom_init(xx_postgres_custom *,xx_io_device *,int64_t);
XXFC_API xx_postgres_custom *xx_postgres_custom_create(xx_io_device *,int64_t);
XXFC_API void xx_postgres_custom_destroy(xx_postgres_custom *);
XXFC_API void xx_postgres_custom_free(xx_postgres_custom *);
XXFC_API bool xx_postgres_custom_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_postgres_custom_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_postgres_custom_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_postgres_custom_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_postgres_custom_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_postgres_custom_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_postgres_custom_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
