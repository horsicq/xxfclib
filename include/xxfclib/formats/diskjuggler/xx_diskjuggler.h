/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_DISKJUGGLER_H
#define XXFCLIB_FORMAT_DISKJUGGLER_H

#include "xxfclib/formats/xx_format.h"

/* DiscJuggler .CDI image.  The descriptor is at the end; members are the stored tracks and pregaps. */
typedef struct xx_diskjuggler {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_diskjuggler;

XXFC_API void xx_diskjuggler_init(xx_diskjuggler *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_diskjuggler *xx_diskjuggler_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_diskjuggler_destroy(xx_diskjuggler *archive);
XXFC_API void xx_diskjuggler_free(xx_diskjuggler *archive);
XXFC_API bool xx_diskjuggler_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_diskjuggler_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_diskjuggler_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_diskjuggler_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_diskjuggler_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_diskjuggler_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_diskjuggler_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_diskjuggler_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_diskjuggler_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
