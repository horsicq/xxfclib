/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_AODOS_H
#define XXFCLIB_FORMAT_AODOS_H

#include "xxfclib/formats/xx_format.h"

typedef struct xx_aodos {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t image_end;
} xx_aodos;

XXFC_API void xx_aodos_init(xx_aodos *image, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_aodos *xx_aodos_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_aodos_destroy(xx_aodos *image);
XXFC_API void xx_aodos_free(xx_aodos *image);
XXFC_API bool xx_aodos_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_aodos_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_aodos_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_aodos_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_aodos_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_aodos_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_aodos_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_aodos_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_aodos_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
