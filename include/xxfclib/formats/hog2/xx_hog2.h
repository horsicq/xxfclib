/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_hog2.h @brief Descent 3 HOG2 archive reader. */

#ifndef XXFCLIB_FORMAT_HOG2_H
#define XXFCLIB_FORMAT_HOG2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Descent 3 HOG2 archive: a 68-byte header, a table of 48-byte
 * (36-byte name, flags, size, timestamp) entries and contiguous stored
 * payloads.
 */
typedef struct xx_hog2 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_hog2;

typedef xx_hog2 xx_hog2_t;

XXFC_API void xx_hog2_init(xx_hog2 *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_hog2 *xx_hog2_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_hog2_destroy(xx_hog2 *archive);
XXFC_API void xx_hog2_free(xx_hog2 *archive);

XXFC_API bool xx_hog2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_hog2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_hog2_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_hog2_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_hog2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hog2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hog2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hog2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hog2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HOG2_H */
