/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lim.h @brief LIM archive reader. */

#ifndef XXFCLIB_FORMAT_LIM_H
#define XXFCLIB_FORMAT_LIM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A LIM archive: an 8-byte header followed by a chain of tagged chunks, where a directory chunk sets the path prefix for the file chunks that follow it.
 */
typedef struct xx_lim {
    Abstractformat format;
    uint64_t number_of_records;
} xx_lim;

typedef xx_lim xx_lim_t;

XXFC_API void xx_lim_init(xx_lim *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_lim *xx_lim_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_lim_destroy(xx_lim *archive);
XXFC_API void xx_lim_free(xx_lim *archive);

XXFC_API bool xx_lim_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_lim_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_lim_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_lim_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lim_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lim_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lim_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lim_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lim_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LIM_H */
