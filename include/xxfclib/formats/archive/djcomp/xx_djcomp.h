/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_DJCOMP_H
#define XXFCLIB_FORMAT_DJCOMP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Single bounded 32-byte DJcomp header and strict LSB LZW member. Filename
 * bytes 8..20 are bounded; 21..31 are opaque, with no checksum authority. */
typedef struct xx_djcomp {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_djcomp;
typedef struct xx_djcomp xx_djcomp_t;
XXFC_API void xx_djcomp_init(xx_djcomp *, xx_io_device *, int64_t);
XXFC_API xx_djcomp *xx_djcomp_create(xx_io_device *, int64_t);
XXFC_API void xx_djcomp_destroy(xx_djcomp *);
XXFC_API void xx_djcomp_free(xx_djcomp *);
XXFC_API bool xx_djcomp_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_djcomp_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_djcomp_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_djcomp_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_djcomp_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_djcomp_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_djcomp_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_djcomp_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_djcomp_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_djcomp_fast_detect(xx_io_device *, int64_t, bool);
XXFC_API xx_file_type_t xx_djcomp_file_type(xx_io_device *, int64_t, bool);
XXFC_API int64_t xx_djcomp_size(xx_io_device *, int64_t, bool);
XXFC_API Abstractextractor *xx_djcomp_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_djcomp_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
