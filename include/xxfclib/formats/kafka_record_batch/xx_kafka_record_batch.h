/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_KAFKA_RECORD_BATCH_H
#define XX_KAFKA_RECORD_BATCH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_kafka_record_batch { Abstractformat format; } xx_kafka_record_batch;
XXFC_API void xx_kafka_record_batch_init(xx_kafka_record_batch *,xx_io_device *,int64_t);
XXFC_API xx_kafka_record_batch *xx_kafka_record_batch_create(xx_io_device *,int64_t);
XXFC_API void xx_kafka_record_batch_destroy(xx_kafka_record_batch *);
XXFC_API void xx_kafka_record_batch_free(xx_kafka_record_batch *);
XXFC_API bool xx_kafka_record_batch_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_kafka_record_batch_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_kafka_record_batch_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_kafka_record_batch_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_kafka_record_batch_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_kafka_record_batch_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_kafka_record_batch_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
