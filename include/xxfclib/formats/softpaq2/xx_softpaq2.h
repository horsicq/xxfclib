/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_SOFTPAQ2_H
#define XXFCLIB_FORMAT_SOFTPAQ2_H

#include "xxfclib/formats/xx_format.h"

/* Compaq/HP SoftPaq distribution container.  A "[FIT]" locator anywhere in
 * the file points at two appended directories: 23-byte stored entries and
 * 38-byte entries that are either stored or PKWARE DCL imploded. */
typedef struct xx_softpaq2 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t locator_offset;
    int64_t directory_offset;
    int64_t split_offset;
} xx_softpaq2;

XXFC_API void xx_softpaq2_init(xx_softpaq2 *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_softpaq2 *xx_softpaq2_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_softpaq2_destroy(xx_softpaq2 *archive);
XXFC_API void xx_softpaq2_free(xx_softpaq2 *archive);
XXFC_API bool xx_softpaq2_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_softpaq2_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_softpaq2_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_softpaq2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_softpaq2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_softpaq2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_softpaq2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_softpaq2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_softpaq2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
