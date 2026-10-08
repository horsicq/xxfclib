/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_LEVELDB_LOG_H
#define XX_LEVELDB_LOG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_leveldb_log { Abstractformat format; } xx_leveldb_log;
XXFC_API void xx_leveldb_log_init(xx_leveldb_log *,xx_io_device *,int64_t);
XXFC_API xx_leveldb_log *xx_leveldb_log_create(xx_io_device *,int64_t);
XXFC_API void xx_leveldb_log_destroy(xx_leveldb_log *);
XXFC_API void xx_leveldb_log_free(xx_leveldb_log *);
XXFC_API bool xx_leveldb_log_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_leveldb_log_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_leveldb_log_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_leveldb_log_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_leveldb_log_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
