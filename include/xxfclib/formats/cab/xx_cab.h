/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_CAB_H
#define XXFCLIB_FORMAT_CAB_H
#include "xxfclib/formats/xx_format.h"

typedef struct xx_cab {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_cab;

XXFC_API void xx_cab_init(xx_cab *, xx_io_device *, int64_t);
XXFC_API xx_cab *xx_cab_create(xx_io_device *, int64_t);
XXFC_API void xx_cab_destroy(xx_cab *);
XXFC_API void xx_cab_free(xx_cab *);
XXFC_API bool xx_cab_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cab_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_cab_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_cab_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_cab_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_cab_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cab_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cab_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_cab_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
#endif
