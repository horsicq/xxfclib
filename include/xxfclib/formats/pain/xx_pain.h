/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_PAIN_H
#define XXFCLIB_FORMAT_PAIN_H

#include "xxfclib/formats/xx_format.h"

/* CRDATA00 ("PAIN" diskmag) data container. */
typedef struct xx_pain {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_pain;

XXFC_API void xx_pain_init(xx_pain *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_pain *xx_pain_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pain_destroy(xx_pain *archive);
XXFC_API void xx_pain_free(xx_pain *archive);
XXFC_API bool xx_pain_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pain_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pain_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_pain_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_pain_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pain_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pain_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pain_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pain_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
