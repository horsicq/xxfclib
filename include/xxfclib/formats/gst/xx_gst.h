/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_GST_H
#define XXFCLIB_FORMAT_GST_H

#include "xxfclib/formats/xx_format.h"

/* GST Software installer container (Timeworks Publisher and friends).  A file
 * is a bare chain of 32-byte member headers, each followed by its payload;
 * members are either stored or PKWARE DCL ("implode") compressed. */
typedef struct xx_gst {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_gst;

XXFC_API void xx_gst_init(xx_gst *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_gst *xx_gst_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_gst_destroy(xx_gst *archive);
XXFC_API void xx_gst_free(xx_gst *archive);
XXFC_API bool xx_gst_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_gst_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_gst_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_gst_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_gst_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gst_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gst_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gst_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gst_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
