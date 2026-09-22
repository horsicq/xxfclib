/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_BVRP_H
#define XXFCLIB_FORMAT_BVRP_H

#include "xxfclib/formats/xx_format.h"

/* BVRP Software .PAC container; members use LZHUF (lh1). */
typedef struct xx_bvrp {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_bvrp;

XXFC_API void xx_bvrp_init(xx_bvrp *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_bvrp *xx_bvrp_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bvrp_destroy(xx_bvrp *archive);
XXFC_API void xx_bvrp_free(xx_bvrp *archive);
XXFC_API bool xx_bvrp_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bvrp_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bvrp_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_bvrp_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_bvrp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bvrp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bvrp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bvrp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bvrp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
