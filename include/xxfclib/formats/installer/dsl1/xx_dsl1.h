/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_DSL1_H
#define XXFCLIB_FORMAT_DSL1_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Bounded Img3 DSL1: flat ASCII catalog names, stored or strict DCL members.
 * Header metadata is opaque and does not authorize installation actions. */
typedef struct xx_dsl1 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_dsl1;
typedef struct xx_dsl1 xx_dsl1_t;
XXFC_API void xx_dsl1_init(xx_dsl1 *, xx_io_device *, int64_t);
XXFC_API xx_dsl1 *xx_dsl1_create(xx_io_device *, int64_t);
XXFC_API void xx_dsl1_destroy(xx_dsl1 *);
XXFC_API void xx_dsl1_free(xx_dsl1 *);
XXFC_API bool xx_dsl1_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dsl1_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_dsl1_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_dsl1_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_dsl1_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_dsl1_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_dsl1_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_dsl1_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_dsl1_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_dsl1_fast_detect(xx_io_device *, int64_t, bool);
XXFC_API xx_file_type_t xx_dsl1_file_type(xx_io_device *, int64_t, bool);
XXFC_API int64_t xx_dsl1_size(xx_io_device *, int64_t, bool);
XXFC_API Abstractextractor *xx_dsl1_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dsl1_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
