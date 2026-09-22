/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_XLAS_H
#define XXFCLIB_FORMAT_XLAS_H

#include "xxfclib/formats/xx_format.h"

/* XLAS container (Xtreme/DOS-era asset pack); LZSS members. */
typedef struct xx_xlas {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_xlas;

XXFC_API void xx_xlas_init(xx_xlas *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_xlas *xx_xlas_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_xlas_destroy(xx_xlas *archive);
XXFC_API void xx_xlas_free(xx_xlas *archive);
XXFC_API bool xx_xlas_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_xlas_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_xlas_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_xlas_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_xlas_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xlas_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xlas_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xlas_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xlas_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
