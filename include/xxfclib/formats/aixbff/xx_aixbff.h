/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_AIXBFF_H
#define XXFCLIB_FORMAT_AIXBFF_H

#include "xxfclib/formats/xx_format.h"

typedef struct xx_aixbff {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_aixbff;

XXFC_API void xx_aixbff_init(xx_aixbff *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_aixbff *xx_aixbff_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_aixbff_destroy(xx_aixbff *archive);
XXFC_API void xx_aixbff_free(xx_aixbff *archive);
XXFC_API bool xx_aixbff_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_aixbff_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_aixbff_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_aixbff_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_aixbff_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_aixbff_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_aixbff_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_aixbff_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_aixbff_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
