/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_WARC_H
#define XXFCLIB_FORMAT_WARC_H

#include "xxfclib/formats/xx_format.h"

typedef struct xx_warc {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_warc;

XXFC_API void xx_warc_init(xx_warc *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_warc *xx_warc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_warc_destroy(xx_warc *archive);
XXFC_API void xx_warc_free(xx_warc *archive);
XXFC_API bool xx_warc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_warc_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_warc_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_warc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_warc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_warc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_warc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_warc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_warc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
