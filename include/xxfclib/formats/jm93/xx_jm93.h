/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_jm93.h @brief JM93 compressed file reader. */

#ifndef XXFCLIB_FORMAT_JM93_H
#define XXFCLIB_FORMAT_JM93_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A JM93 compressed file: a fixed 0x49-byte header carrying one 60-byte name and the two sizes, followed by a single PKWARE DCL imploded stream that runs to the end of the container.
 */
typedef struct xx_jm93 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_jm93;

typedef xx_jm93 xx_jm93_t;

XXFC_API void xx_jm93_init(xx_jm93 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_jm93 *xx_jm93_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_jm93_destroy(xx_jm93 *archive);
XXFC_API void xx_jm93_free(xx_jm93 *archive);

XXFC_API bool xx_jm93_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_jm93_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_jm93_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_jm93_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_jm93_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_jm93_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_jm93_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_jm93_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_jm93_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_JM93_H */
