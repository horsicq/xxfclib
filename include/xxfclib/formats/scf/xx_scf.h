/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_SCF_H
#define XXFCLIB_FORMAT_SCF_H

#include "xxfclib/formats/xx_format.h"

/* Hijaak/PrintPartner .SCF setup container; members use PKWARE DCL. */
typedef struct xx_scf {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_scf;

XXFC_API void xx_scf_init(xx_scf *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_scf *xx_scf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_scf_destroy(xx_scf *archive);
XXFC_API void xx_scf_free(xx_scf *archive);
XXFC_API bool xx_scf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_scf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_scf_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_scf_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_scf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_scf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_scf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_scf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_scf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
