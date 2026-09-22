/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_psn.h @brief 3M Post-it Software Notes "PSNcompress" packed file reader. */

#ifndef XXFCLIB_FORMAT_PSN_H
#define XXFCLIB_FORMAT_PSN_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A 3M PSNcompress packed file: a 65-byte ASCII header carrying the
 * copyright banner, a version and the plaintext length, followed by one
 * compressed stream.  It wraps exactly one payload, so the record list always
 * holds one entry.
 */
typedef struct xx_psn {
    Abstractformat format;
    uint64_t number_of_records;
} xx_psn;

typedef xx_psn xx_psn_t;

XXFC_API void xx_psn_init(xx_psn *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_psn *xx_psn_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_psn_destroy(xx_psn *archive);
XXFC_API void xx_psn_free(xx_psn *archive);

XXFC_API bool xx_psn_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_psn_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_psn_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_psn_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_psn_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_psn_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_psn_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_psn_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_psn_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PSN_H */
