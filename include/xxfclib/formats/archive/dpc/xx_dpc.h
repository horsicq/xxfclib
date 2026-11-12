/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_DPC_H
#define XXFCLIB_FORMAT_DPC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Strict contiguous Delphi Package Collection with complete zlib streams.
 * Flat ASCII File0..FileN aliases only; metadata is not installation authority. */
typedef struct xx_dpc {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_dpc;
typedef struct xx_dpc xx_dpc_t;
XXFC_API void xx_dpc_init(xx_dpc *, xx_io_device *, int64_t);
XXFC_API xx_dpc *xx_dpc_create(xx_io_device *, int64_t);
XXFC_API void xx_dpc_destroy(xx_dpc *);
XXFC_API void xx_dpc_free(xx_dpc *);
XXFC_API bool xx_dpc_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dpc_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_dpc_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_dpc_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_dpc_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_dpc_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_dpc_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_dpc_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_dpc_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dpc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dpc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dpc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dpc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dpc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
