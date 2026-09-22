/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_TWOIMG_H
#define XXFCLIB_FORMAT_TWOIMG_H

#include "xxfclib/formats/xx_format.h"

/* Apple II 2IMG disk image: a 64-byte header with image, comment and creator extents. */
typedef struct xx_twoimg {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_twoimg;

XXFC_API void xx_twoimg_init(xx_twoimg *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_twoimg *xx_twoimg_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_twoimg_destroy(xx_twoimg *archive);
XXFC_API void xx_twoimg_free(xx_twoimg *archive);
XXFC_API bool xx_twoimg_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_twoimg_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_twoimg_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_twoimg_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_twoimg_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_twoimg_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_twoimg_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_twoimg_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_twoimg_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
