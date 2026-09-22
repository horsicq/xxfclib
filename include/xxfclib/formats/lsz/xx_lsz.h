/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lsz.h @brief Delrina WinFax LSZ library reader. */

#ifndef XXFCLIB_FORMAT_LSZ_H
#define XXFCLIB_FORMAT_LSZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Delrina WinFax LSZ library: a six-byte magic-and-version header followed by a chain of 51-byte records, each immediately followed by its stored or PKWARE DCL imploded payload.
 */
typedef struct xx_lsz {
    Abstractformat format;
    uint64_t number_of_records;
} xx_lsz;

typedef xx_lsz xx_lsz_t;

XXFC_API void xx_lsz_init(xx_lsz *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_lsz *xx_lsz_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_lsz_destroy(xx_lsz *archive);
XXFC_API void xx_lsz_free(xx_lsz *archive);

XXFC_API bool xx_lsz_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_lsz_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_lsz_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_lsz_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lsz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lsz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lsz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lsz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lsz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LSZ_H */
