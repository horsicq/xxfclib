/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_hog.h @brief A Descent 1/2 "DHF" HOG archive: a 3-byte signature followed by a chain of 17-byte headers (13-byte name, 32-bit size) each immediately followed by its stored payload. */

#ifndef XXFCLIB_FORMAT_HOG_H
#define XXFCLIB_FORMAT_HOG_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Descent 1/2 "DHF" HOG archive: a 3-byte signature followed by a chain of 17-byte headers (13-byte name, 32-bit size) each immediately followed by its stored payload.
 */
typedef struct xx_hog {
    Abstractformat format;
    uint64_t number_of_records;
} xx_hog;

typedef xx_hog xx_hog_t;

XXFC_API void xx_hog_init(xx_hog *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_hog *xx_hog_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_hog_destroy(xx_hog *archive);
XXFC_API void xx_hog_free(xx_hog *archive);

XXFC_API bool xx_hog_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_hog_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_hog_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_hog_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_hog_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hog_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hog_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hog_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hog_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HOG_H */
