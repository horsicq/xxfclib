/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_UBJSON_H
#define XX_UBJSON_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ubjson { Abstractformat format; } xx_ubjson;
XXFC_API void xx_ubjson_init(xx_ubjson *,xx_io_device *,int64_t);
XXFC_API xx_ubjson *xx_ubjson_create(xx_io_device *,int64_t);
XXFC_API void xx_ubjson_destroy(xx_ubjson *);
XXFC_API void xx_ubjson_free(xx_ubjson *);
XXFC_API bool xx_ubjson_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ubjson_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ubjson_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ubjson_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ubjson_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ubjson_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ubjson_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
