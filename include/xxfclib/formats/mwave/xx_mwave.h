/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_MWAVE_H
#define XXFCLIB_FORMAT_MWAVE_H

#include "xxfclib/formats/xx_format.h"

/* IBM Mwave DSP distribution file: a 24-byte header carrying the original
 * 8.3 name and a DOS timestamp, followed by a Unix compress (LZW) stream
 * whose flags byte is kept in the header rather than ahead of the data. */
typedef struct xx_mwave {
    Abstractformat format;
    uint64_t uncompressed_size;
    uint16_t dos_time;
    uint16_t dos_date;
    uint8_t compress_flags;
    char name[13];
    int64_t stream_end;
} xx_mwave;

XXFC_API void xx_mwave_init(xx_mwave *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_mwave *xx_mwave_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_mwave_destroy(xx_mwave *archive);
XXFC_API void xx_mwave_free(xx_mwave *archive);
XXFC_API bool xx_mwave_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mwave_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_mwave_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_mwave_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_mwave_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mwave_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mwave_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mwave_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mwave_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mwave_unpack_to_device(xx_mwave *archive,
                                        xx_io_device *destination,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_mwave_get_uncompressed_size(const xx_mwave *archive);
XXFC_API int64_t xx_mwave_get_stream_end(const xx_mwave *archive);

#endif
