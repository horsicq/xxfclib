/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_SMSIPAK_H
#define XXFCLIB_FORMAT_SMSIPAK_H

#include "xxfclib/formats/xx_format.h"

/* "SMSIPAK " distribution media.  Members are either stored or wrapped in a
 * PKWARE Data Compression Library (DCL) stream. */
typedef struct xx_smsipak {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_smsipak;

XXFC_API void xx_smsipak_init(xx_smsipak *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_smsipak *xx_smsipak_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_smsipak_destroy(xx_smsipak *archive);
XXFC_API void xx_smsipak_free(xx_smsipak *archive);
XXFC_API bool xx_smsipak_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_smsipak_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_smsipak_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_smsipak_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_smsipak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_smsipak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_smsipak_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_smsipak_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_smsipak_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
