/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_KRYOFLUX_STREAM_H
#define XXFCLIB_FORMAT_KRYOFLUX_STREAM_H

#include "xxfclib/formats/xx_format.h"

/* KryoFlux stream file (trackNN.S.raw): the flux transitions of one side of
 * one track.  The member is the track's sectors, decoded from IBM MFM or
 * Amiga MFM. */
typedef struct xx_kryoflux_stream {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_kryoflux_stream;

XXFC_API void xx_kryoflux_stream_init(xx_kryoflux_stream *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_kryoflux_stream *xx_kryoflux_stream_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_kryoflux_stream_destroy(xx_kryoflux_stream *archive);
XXFC_API void xx_kryoflux_stream_free(xx_kryoflux_stream *archive);
XXFC_API bool xx_kryoflux_stream_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_kryoflux_stream_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_kryoflux_stream_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_kryoflux_stream_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *
xx_kryoflux_stream_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_kryoflux_stream_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_kryoflux_stream_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_kryoflux_stream_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_kryoflux_stream_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
