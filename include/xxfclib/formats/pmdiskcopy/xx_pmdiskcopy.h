/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_PMDISKCOPY_H
#define XXFCLIB_FORMAT_PMDISKCOPY_H

#include "xxfclib/formats/xx_format.h"

/* PM Diskcopy image: an 11-byte signature, a DOS BPB copy, then the stored sectors. */
typedef struct xx_pmdiskcopy {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_pmdiskcopy;

XXFC_API void xx_pmdiskcopy_init(xx_pmdiskcopy *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_pmdiskcopy *xx_pmdiskcopy_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pmdiskcopy_destroy(xx_pmdiskcopy *archive);
XXFC_API void xx_pmdiskcopy_free(xx_pmdiskcopy *archive);
XXFC_API bool xx_pmdiskcopy_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pmdiskcopy_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pmdiskcopy_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_pmdiskcopy_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_pmdiskcopy_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pmdiskcopy_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pmdiskcopy_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pmdiskcopy_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pmdiskcopy_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
