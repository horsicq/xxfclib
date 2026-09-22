/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_JASC_H
#define XXFCLIB_FORMAT_JASC_H

#include "xxfclib/formats/xx_format.h"

/* Jasc Software installer archive (.CMP, and the SETUP.INF that ships beside
 * it).  A headerless chain of members, each an LHA -lh5- stream. */
typedef struct xx_jasc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_jasc;

XXFC_API void xx_jasc_init(xx_jasc *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_jasc *xx_jasc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_jasc_destroy(xx_jasc *archive);
XXFC_API void xx_jasc_free(xx_jasc *archive);
XXFC_API bool xx_jasc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_jasc_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_jasc_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_jasc_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_jasc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_jasc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_jasc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_jasc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_jasc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
