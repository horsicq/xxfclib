/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_MONGODB_BSON_H
#define XX_MONGODB_BSON_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mongodb_bson { Abstractformat format; } xx_mongodb_bson;
XXFC_API void xx_mongodb_bson_init(xx_mongodb_bson *,xx_io_device *,int64_t);
XXFC_API xx_mongodb_bson *xx_mongodb_bson_create(xx_io_device *,int64_t);
XXFC_API void xx_mongodb_bson_destroy(xx_mongodb_bson *);
XXFC_API void xx_mongodb_bson_free(xx_mongodb_bson *);
XXFC_API bool xx_mongodb_bson_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mongodb_bson_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mongodb_bson_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mongodb_bson_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mongodb_bson_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mongodb_bson_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mongodb_bson_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
