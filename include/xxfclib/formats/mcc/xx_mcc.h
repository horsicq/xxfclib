/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_MCC_H
#define XXFCLIB_FORMAT_MCC_H

#include "xxfclib/formats/xx_format.h"

/* MCC registration container (.REG): a flat chain of 34-byte "MCC" member
 * headers, each immediately followed by its payload.  Method 0 is stored;
 * method 1 is an unidentified bit-oriented codec and fails closed. */
typedef struct xx_mcc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_mcc;

XXFC_API void xx_mcc_init(xx_mcc *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_mcc *xx_mcc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_mcc_destroy(xx_mcc *archive);
XXFC_API void xx_mcc_free(xx_mcc *archive);
XXFC_API bool xx_mcc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mcc_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mcc_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_mcc_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_mcc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mcc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mcc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mcc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mcc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
