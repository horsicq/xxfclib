/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_LIFKD_H
#define XXFCLIB_FORMAT_LIFKD_H

#include "xxfclib/formats/xx_format.h"

/* Knowledge Dynamics Corp. ".LIF" installer library (NOT HP's LIF disk
 * format, and unrelated to the 0x8000 word that names that one).  The file is
 * a bare chain of members: a 54-byte header whose first 34 bytes are ASCII
 * hex digits, followed immediately by the member's packed bytes.  There is no
 * archive header, no directory and no end marker, so identification is purely
 * structural: the chain has to land exactly on end of file.
 *
 * Method 1 is stored, method 2 is Rahul Dhesi's LZD (the same variable-width
 * LZW that ZOO uses for its method 1). */
typedef struct xx_lifkd {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_lifkd;

/* Detector gate: true when @p bytes (at least 54 of them) are one complete,
 * well-formed member header -- 34 hex digits, method "01" or "02", stored
 * sizes equal for method 1, and a printable name terminated and zero padded
 * inside its 20-byte field.  Pure, allocation-free and reads only @p bytes;
 * the full chain walk is still check_is_valid's job. */
XXFC_API bool xx_lifkd_is_member_header(const uint8_t *bytes, size_t size);

XXFC_API void xx_lifkd_init(xx_lifkd *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_lifkd *xx_lifkd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_lifkd_destroy(xx_lifkd *archive);
XXFC_API void xx_lifkd_free(xx_lifkd *archive);
XXFC_API bool xx_lifkd_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lifkd_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_lifkd_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_lifkd_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_lifkd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lifkd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lifkd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lifkd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lifkd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
