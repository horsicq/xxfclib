/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_ROCKSDB_BLOB_H
#define XX_ROCKSDB_BLOB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_rocksdb_blob { Abstractformat format; } xx_rocksdb_blob;
XXFC_API void xx_rocksdb_blob_init(xx_rocksdb_blob *,xx_io_device *,int64_t);
XXFC_API xx_rocksdb_blob *xx_rocksdb_blob_create(xx_io_device *,int64_t);
XXFC_API void xx_rocksdb_blob_destroy(xx_rocksdb_blob *);
XXFC_API void xx_rocksdb_blob_free(xx_rocksdb_blob *);
XXFC_API bool xx_rocksdb_blob_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_rocksdb_blob_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_rocksdb_blob_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_rocksdb_blob_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_rocksdb_blob_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
