/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only original Xbox FATX partition image reader.
 *
 * Supports FATX16/FATX32 cluster chains, nested directories, and exact file
 * extraction from a volume whose superblock starts at base_address. Whole
 * Xbox HDD partition maps, encrypted volumes, and damaged-chain recovery are
 * outside this reader.
 *
 * Layout references (this is an independent implementation):
 * https://xboxdevwiki.net/FATX
 * https://github.com/mborgerson/fatx/tree/master/libfatx
 */
#ifndef XXFCLIB_FORMAT_FATX_H
#define XXFCLIB_FORMAT_FATX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_fatx_s {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t bytes_per_cluster;
    uint32_t root_cluster;
    uint8_t fat_width;
} xx_fatx;
typedef xx_fatx xx_fatx_t;
typedef xx_fatx XFATX;

XXFC_API void xx_fatx_init(xx_fatx *, xx_io_device *, int64_t);
XXFC_API xx_fatx *xx_fatx_create(xx_io_device *, int64_t);
XXFC_API void xx_fatx_destroy(xx_fatx *);
XXFC_API void xx_fatx_free(xx_fatx *);
XXFC_API bool xx_fatx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_fatx_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_fatx_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_fatx_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_fatx_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_fatx_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_fatx_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_fatx_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_fatx_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_fatx_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_fatx_to_format(xx_fatx *v) { return v ? &v->format : NULL; }

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_fatx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_fatx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_fatx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_fatx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_fatx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
