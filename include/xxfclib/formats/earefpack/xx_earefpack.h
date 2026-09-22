/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_earefpack.h @brief Electronic Arts RefPack container reader. */

#ifndef XXFCLIB_FORMAT_EAREFPACK_H
#define XXFCLIB_FORMAT_EAREFPACK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Electronic Arts RefPack (QFS) container: a two-byte signature word, big-endian size fields, and a single LZ77 command stream that must run from the header to an explicit terminator landing exactly on EOF.
 */
typedef struct xx_earefpack {
    Abstractformat format;
    uint64_t number_of_records;
} xx_earefpack;

typedef xx_earefpack xx_earefpack_t;

XXFC_API void xx_earefpack_init(xx_earefpack *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_earefpack *xx_earefpack_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_earefpack_destroy(xx_earefpack *archive);
XXFC_API void xx_earefpack_free(xx_earefpack *archive);

XXFC_API bool xx_earefpack_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_earefpack_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_earefpack_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_earefpack_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_earefpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_earefpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_earefpack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_earefpack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_earefpack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EAREFPACK_H */
