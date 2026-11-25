/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_LEVELDB_SSTABLE_H
#define XX_LEVELDB_SSTABLE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_leveldb_sstable {
    Abstractformat format;
} xx_leveldb_sstable;
XXFC_API void xx_leveldb_sstable_init(xx_leveldb_sstable *, xx_io_device *, int64_t);
XXFC_API xx_leveldb_sstable *xx_leveldb_sstable_create(xx_io_device *, int64_t);
XXFC_API void xx_leveldb_sstable_destroy(xx_leveldb_sstable *);
XXFC_API void xx_leveldb_sstable_free(xx_leveldb_sstable *);
XXFC_API bool xx_leveldb_sstable_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_leveldb_sstable_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_leveldb_sstable_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_leveldb_sstable_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_leveldb_sstable_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_leveldb_sstable_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_leveldb_sstable_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
