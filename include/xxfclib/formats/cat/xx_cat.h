/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_CAT_H
#define XXFCLIB_FORMAT_CAT_H

#include "xxfclib/formats/xx_format.h"

/* Software Creations / Bullfrog-era .CAT catalogue: a flat stored directory. */
typedef struct xx_cat {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_cat;

XXFC_API void xx_cat_init(xx_cat *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_cat *xx_cat_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cat_destroy(xx_cat *archive);
XXFC_API void xx_cat_free(xx_cat *archive);
XXFC_API bool xx_cat_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cat_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cat_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_cat_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_cat_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cat_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cat_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cat_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cat_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
