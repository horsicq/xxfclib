/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zoo.h @brief ZOO archive reader. */

#ifndef XXFCLIB_FORMAT_ZOO_H
#define XXFCLIB_FORMAT_ZOO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZOO archive: a fixed 34-byte archive header pointing at a singly linked chain of directory entries, each of which points in turn at its member's data anywhere in the file.
 */
typedef struct xx_zoo {
    Abstractformat format;
    uint64_t number_of_records;
} xx_zoo;

typedef xx_zoo xx_zoo_t;

XXFC_API void xx_zoo_init(xx_zoo *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_zoo *xx_zoo_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_zoo_destroy(xx_zoo *archive);
XXFC_API void xx_zoo_free(xx_zoo *archive);

XXFC_API bool xx_zoo_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_zoo_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_zoo_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_zoo_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zoo_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zoo_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zoo_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zoo_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zoo_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZOO_H */
