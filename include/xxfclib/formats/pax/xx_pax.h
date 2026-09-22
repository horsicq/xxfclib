/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_PAX_H
#define XXFCLIB_FORMAT_PAX_H

#include "xxfclib/formats/xx_format.h"

/* GEM-View PAX ("LZF0") member chain.  Every member carries an adaptive
 * Huffman + LZ77 stream; there is no POSIX pax relationship whatsoever. */
typedef struct xx_pax {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_pax;

XXFC_API void xx_pax_init(xx_pax *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_pax *xx_pax_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pax_destroy(xx_pax *archive);
XXFC_API void xx_pax_free(xx_pax *archive);
XXFC_API bool xx_pax_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pax_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pax_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_pax_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_pax_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pax_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pax_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pax_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pax_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
