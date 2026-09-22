/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_chieflzmulti.h @brief ChiefLZ Multiple archive reader. */

#ifndef XXFCLIB_FORMAT_CHIEFLZMULTI_H
#define XXFCLIB_FORMAT_CHIEFLZMULTI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ChiefLZ Multiple archive: a fixed 0x7e-byte preamble, a table of 0x29-byte entries, one packed name block, then every member's data laid end to end.
 */
typedef struct xx_chieflzmulti {
    Abstractformat format;
    uint64_t number_of_records;
} xx_chieflzmulti;

typedef xx_chieflzmulti xx_chieflzmulti_t;

XXFC_API void xx_chieflzmulti_init(xx_chieflzmulti *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_chieflzmulti *xx_chieflzmulti_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_chieflzmulti_destroy(xx_chieflzmulti *archive);
XXFC_API void xx_chieflzmulti_free(xx_chieflzmulti *archive);

XXFC_API bool xx_chieflzmulti_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_chieflzmulti_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_chieflzmulti_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_chieflzmulti_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_chieflzmulti_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_chieflzmulti_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_chieflzmulti_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_chieflzmulti_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_chieflzmulti_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CHIEFLZMULTI_H */
