/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_FINEAR_H
#define XXFCLIB_FORMAT_FINEAR_H

#include "xxfclib/formats/xx_format.h"

/* "FINEAR" installer transport: a 17-byte header followed by a single
 * LZHUF (LHA -lh1-) stream.  One member, no stored name. */
typedef struct xx_finear {
    Abstractformat format;
    uint64_t uncompressed_size;
    uint32_t checksum;
    int64_t stream_end;
} xx_finear;

XXFC_API void xx_finear_init(xx_finear *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_finear *xx_finear_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_finear_destroy(xx_finear *archive);
XXFC_API void xx_finear_free(xx_finear *archive);
XXFC_API bool xx_finear_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_finear_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_finear_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_finear_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_finear_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_finear_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_finear_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_finear_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_finear_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_finear_unpack_to_device(xx_finear *archive,
                                         xx_io_device *destination,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_finear_get_uncompressed_size(const xx_finear *archive);
XXFC_API int64_t xx_finear_get_stream_end(const xx_finear *archive);

#endif
