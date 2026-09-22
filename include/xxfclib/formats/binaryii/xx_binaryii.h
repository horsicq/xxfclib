/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_BINARYII_H
#define XXFCLIB_FORMAT_BINARYII_H

#include "xxfclib/formats/xx_format.h"

/* Apple II Binary II (.bny/.bxy/.sdk): a chain of 128-byte headers, each
 * followed by its member's bytes padded up to a 128-byte boundary.  Nothing
 * is ever compressed. */
typedef struct xx_binaryii {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint8_t version;
} xx_binaryii;

XXFC_API void xx_binaryii_init(xx_binaryii *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_binaryii *xx_binaryii_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_binaryii_destroy(xx_binaryii *archive);
XXFC_API void xx_binaryii_free(xx_binaryii *archive);
XXFC_API bool xx_binaryii_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_binaryii_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_binaryii_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_binaryii_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_binaryii_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_binaryii_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_binaryii_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_binaryii_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_binaryii_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
