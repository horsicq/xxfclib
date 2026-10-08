/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_HADOOP_SEQUENCEFILE_H
#define XX_HADOOP_SEQUENCEFILE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hadoop_sequencefile { Abstractformat format; } xx_hadoop_sequencefile;
XXFC_API void xx_hadoop_sequencefile_init(xx_hadoop_sequencefile *,xx_io_device *,int64_t);
XXFC_API xx_hadoop_sequencefile *xx_hadoop_sequencefile_create(xx_io_device *,int64_t);
XXFC_API void xx_hadoop_sequencefile_destroy(xx_hadoop_sequencefile *);
XXFC_API void xx_hadoop_sequencefile_free(xx_hadoop_sequencefile *);
XXFC_API bool xx_hadoop_sequencefile_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_hadoop_sequencefile_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_hadoop_sequencefile_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_hadoop_sequencefile_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_hadoop_sequencefile_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
