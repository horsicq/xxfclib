/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zpak.h @brief ZSoft ZPAK archive reader. */

#ifndef XXFCLIB_FORMAT_ZPAK_H
#define XXFCLIB_FORMAT_ZPAK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZSoft ZPAK archive: a six-byte header with a member count, a fixed 33-byte directory record per member, and member payloads at absolute offsets, compressed with either chunk-framed LZW or PKWARE DCL depending on the magic.
 */
typedef struct xx_zpak {
    Abstractformat format;
    uint64_t number_of_records;
} xx_zpak;

typedef xx_zpak xx_zpak_t;

XXFC_API void xx_zpak_init(xx_zpak *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_zpak *xx_zpak_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_zpak_destroy(xx_zpak *archive);
XXFC_API void xx_zpak_free(xx_zpak *archive);

XXFC_API bool xx_zpak_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_zpak_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_zpak_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_zpak_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zpak_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zpak_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zpak_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZPAK_H */
