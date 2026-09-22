/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_BCW_H
#define XXFCLIB_FORMAT_BCW_H

#include "xxfclib/formats/xx_format.h"

/* BCW container (SmartBoard-era installer payload); identification only. */
typedef struct xx_bcw {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_bcw;

XXFC_API void xx_bcw_init(xx_bcw *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_bcw *xx_bcw_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bcw_destroy(xx_bcw *archive);
XXFC_API void xx_bcw_free(xx_bcw *archive);
XXFC_API bool xx_bcw_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bcw_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bcw_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_bcw_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_bcw_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bcw_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bcw_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bcw_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bcw_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
