/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ibmspack.h @brief IBM install-diskette packed file reader. */

#ifndef XXFCLIB_FORMAT_IBMSPACK_H
#define XXFCLIB_FORMAT_IBMSPACK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IBM install-diskette packed file: a single member held as one bare FLS-LZ adaptive-phrase stream that begins with its own 'S' tag byte and ends exactly at end-of-file.
 */
typedef struct xx_ibmspack {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ibmspack;

typedef xx_ibmspack xx_ibmspack_t;

XXFC_API void xx_ibmspack_init(xx_ibmspack *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ibmspack *xx_ibmspack_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ibmspack_destroy(xx_ibmspack *archive);
XXFC_API void xx_ibmspack_free(xx_ibmspack *archive);

XXFC_API bool xx_ibmspack_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ibmspack_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ibmspack_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ibmspack_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ibmspack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ibmspack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ibmspack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ibmspack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ibmspack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IBMSPACK_H */
