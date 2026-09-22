/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_DISKDUPE_H
#define XXFCLIB_FORMAT_DISKDUPE_H

#include "xxfclib/formats/xx_format.h"

/* DiskDupe DDI image.  A per-track block table; the member is the reassembled flat image. */
typedef struct xx_diskdupe {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_diskdupe;

XXFC_API void xx_diskdupe_init(xx_diskdupe *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_diskdupe *xx_diskdupe_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_diskdupe_destroy(xx_diskdupe *archive);
XXFC_API void xx_diskdupe_free(xx_diskdupe *archive);
XXFC_API bool xx_diskdupe_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_diskdupe_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_diskdupe_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_diskdupe_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_diskdupe_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_diskdupe_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_diskdupe_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_diskdupe_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_diskdupe_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
