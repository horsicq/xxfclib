/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_RED_H
#define XXFCLIB_FORMAT_RED_H

#include "xxfclib/formats/xx_format.h"

/* Knowledge Dynamics RED installer archive (also its newer ".LIF" volumes):
 * a flat chain of "RR" members, stored or LHA "-lh5-" over CRC-segmented
 * compressed data. */
typedef struct xx_red {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_red;

XXFC_API void xx_red_init(xx_red *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_red *xx_red_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_red_destroy(xx_red *archive);
XXFC_API void xx_red_free(xx_red *archive);
XXFC_API bool xx_red_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_red_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_red_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_red_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_red_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_red_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_red_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_red_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_red_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
