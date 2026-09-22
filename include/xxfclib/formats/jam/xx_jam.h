/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_jam.h @brief JAM resource archive reader. */

#ifndef XXFCLIB_FORMAT_JAM_H
#define XXFCLIB_FORMAT_JAM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A JAM resource archive: a three-byte magic followed by a directory tree of fixed-size records, every member stored verbatim.
 */
typedef struct xx_jam {
    Abstractformat format;
    uint64_t number_of_records;
} xx_jam;

typedef xx_jam xx_jam_t;

XXFC_API void xx_jam_init(xx_jam *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_jam *xx_jam_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_jam_destroy(xx_jam *archive);
XXFC_API void xx_jam_free(xx_jam *archive);

XXFC_API bool xx_jam_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_jam_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_jam_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_jam_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_jam_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_jam_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_jam_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_jam_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_jam_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_JAM_H */
