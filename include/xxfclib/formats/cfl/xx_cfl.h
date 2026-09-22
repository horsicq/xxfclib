/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cfl.h @brief CFL archive reader. */

#ifndef XXFCLIB_FORMAT_CFL_H
#define XXFCLIB_FORMAT_CFL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A CFL3 archive: a twelve-byte header pointing at a directory block that is itself either stored or zlib packed, listing members that are stored verbatim or held as headerless zlib streams.
 */
typedef struct xx_cfl {
    Abstractformat format;
    uint64_t number_of_records;
} xx_cfl;

typedef xx_cfl xx_cfl_t;

XXFC_API void xx_cfl_init(xx_cfl *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_cfl *xx_cfl_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_cfl_destroy(xx_cfl *archive);
XXFC_API void xx_cfl_free(xx_cfl *archive);

XXFC_API bool xx_cfl_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_cfl_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_cfl_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_cfl_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cfl_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cfl_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cfl_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cfl_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cfl_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CFL_H */
