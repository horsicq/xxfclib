/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_DH_H
#define XXFCLIB_FORMAT_DH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Bounded DH0902 32-byte records with standalone PKWARE DCL payloads.
 * Other header variants and unsafe/colliding output names fail closed.
 * No interpretation of unknown header words as checksums is claimed. */
typedef struct xx_dh {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_dh;
typedef struct xx_dh xx_dh_t;
XXFC_API void xx_dh_init(xx_dh *, xx_io_device *, int64_t);
XXFC_API xx_dh *xx_dh_create(xx_io_device *, int64_t);
XXFC_API void xx_dh_destroy(xx_dh *);
XXFC_API void xx_dh_free(xx_dh *);
XXFC_API bool xx_dh_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dh_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_dh_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_dh_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_dh_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_dh_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_dh_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_dh_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_dh_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_dh_fast_detect(xx_io_device *, int64_t, bool);
XXFC_API xx_file_type_t xx_dh_file_type(xx_io_device *, int64_t, bool);
XXFC_API int64_t xx_dh_size(xx_io_device *, int64_t, bool);
XXFC_API Abstractextractor *xx_dh_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dh_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
