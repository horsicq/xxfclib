/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_CPOINT_H
#define XXFCLIB_FORMAT_CPOINT_H

#include "xxfclib/formats/xx_format.h"

/* cPoint .INS installer with stored type-3 file records. */
typedef struct xx_cpoint {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_cpoint;

XXFC_API void xx_cpoint_init(xx_cpoint *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_cpoint *xx_cpoint_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cpoint_destroy(xx_cpoint *archive);
XXFC_API void xx_cpoint_free(xx_cpoint *archive);
XXFC_API bool xx_cpoint_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cpoint_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cpoint_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_cpoint_get_number_of_archive_records(Abstractformat *self,
                                                            xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_cpoint_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cpoint_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cpoint_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cpoint_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cpoint_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
