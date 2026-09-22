/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_mi10.h @brief MI10 crunched archive reader. */

#ifndef XXFCLIB_FORMAT_MI10_H
#define XXFCLIB_FORMAT_MI10_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An MI10 archive: a chain of 16-byte big-endian block headers, each followed by its backward-decoded LZ stream, tiling the file exactly apart from at most two 0x6b pad bytes between blocks.
 */
typedef struct xx_mi10 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_mi10;

typedef xx_mi10 xx_mi10_t;

XXFC_API void xx_mi10_init(xx_mi10 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_mi10 *xx_mi10_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_mi10_destroy(xx_mi10 *archive);
XXFC_API void xx_mi10_free(xx_mi10 *archive);

XXFC_API bool xx_mi10_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_mi10_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_mi10_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_mi10_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mi10_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mi10_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mi10_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mi10_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mi10_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MI10_H */
