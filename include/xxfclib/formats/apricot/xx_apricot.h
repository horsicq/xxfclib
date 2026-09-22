/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_apricot.h @brief An ACT Apricot disk image: a 0x80-byte preamble followed by a chain of 16-byte tagged chunks holding literal and run-length records. */

#ifndef XXFCLIB_FORMAT_APRICOT_H
#define XXFCLIB_FORMAT_APRICOT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An ACT Apricot disk image: a 0x80-byte preamble followed by a chain of 16-byte tagged chunks holding literal and run-length records.
 */
typedef struct xx_apricot {
    Abstractformat format;
    uint64_t number_of_records;
} xx_apricot;

typedef xx_apricot xx_apricot_t;

XXFC_API void xx_apricot_init(xx_apricot *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_apricot *xx_apricot_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_apricot_destroy(xx_apricot *archive);
XXFC_API void xx_apricot_free(xx_apricot *archive);

XXFC_API bool xx_apricot_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apricot_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_apricot_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_apricot_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_apricot_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_apricot_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_apricot_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_apricot_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_apricot_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_APRICOT_H */
