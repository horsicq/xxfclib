/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_terse.h @brief IBM TERSE archive reader. */

#ifndef XXFCLIB_FORMAT_TERSE_H
#define XXFCLIB_FORMAT_TERSE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IBM TERSE container: a 4- or 12-byte header followed by one 12-bit MSB-first code stream holding a single tersed data set, whose decoded length is stored nowhere and has to be measured.
 */
typedef struct xx_terse {
    Abstractformat format;
    uint64_t number_of_records;
} xx_terse;

typedef xx_terse xx_terse_t;

XXFC_API void xx_terse_init(xx_terse *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_terse *xx_terse_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_terse_destroy(xx_terse *archive);
XXFC_API void xx_terse_free(xx_terse *archive);

XXFC_API bool xx_terse_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_terse_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_terse_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_terse_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_terse_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_terse_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_terse_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_terse_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_terse_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TERSE_H */
