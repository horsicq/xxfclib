/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_BINHEX_H
#define XXFCLIB_FORMAT_BINHEX_H

#include "xxfclib/formats/xx_format.h"

/* BinHex 4.0 (.hqx): a printable-text wrapper around a Macintosh file.  The
 * payload is 8-to-6 encoded, RLE90 compressed underneath, and carries a
 * CRC-16/XMODEM over the header and over each of the two forks. */
typedef struct xx_binhex {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    bool complete;
} xx_binhex;

XXFC_API void xx_binhex_init(xx_binhex *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_binhex *xx_binhex_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_binhex_destroy(xx_binhex *archive);
XXFC_API void xx_binhex_free(xx_binhex *archive);
XXFC_API bool xx_binhex_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_binhex_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_binhex_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_binhex_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_binhex_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_binhex_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_binhex_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_binhex_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_binhex_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
