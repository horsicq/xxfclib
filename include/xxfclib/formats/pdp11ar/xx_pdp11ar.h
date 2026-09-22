/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_PDP11AR_H
#define XXFCLIB_FORMAT_PDP11AR_H

#include "xxfclib/formats/xx_format.h"

/* UNIX V7 / 2BSD PDP-11's binary ar pre-dates the ASCII !<arch> format. */
typedef struct xx_pdp11ar {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_pdp11ar;

XXFC_API void xx_pdp11ar_init(xx_pdp11ar *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_pdp11ar *xx_pdp11ar_create(xx_io_device *device,
                                        int64_t base_address);
XXFC_API void xx_pdp11ar_destroy(xx_pdp11ar *archive);
XXFC_API void xx_pdp11ar_free(xx_pdp11ar *archive);
XXFC_API bool xx_pdp11ar_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_pdp11ar_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_pdp11ar_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_pdp11ar_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_pdp11ar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pdp11ar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pdp11ar_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pdp11ar_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pdp11ar_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
