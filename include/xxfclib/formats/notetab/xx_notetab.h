/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_NOTETAB_H
#define XXFCLIB_FORMAT_NOTETAB_H

#include "xxfclib/formats/xx_format.h"

/* NoteTab (Fookes Software) Clipbook Library / Outline document (.clb /
 * .otl / .clh).  A plain-text container: nothing is compressed, each clip is
 * a stored run of lines opened by an H="name" heading. */
typedef struct xx_notetab {
    Abstractformat format;
    uint64_t number_of_records;
} xx_notetab;

XXFC_API void xx_notetab_init(xx_notetab *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_notetab *xx_notetab_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_notetab_destroy(xx_notetab *archive);
XXFC_API void xx_notetab_free(xx_notetab *archive);
XXFC_API bool xx_notetab_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_notetab_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_notetab_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_notetab_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_notetab_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_notetab_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_notetab_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_notetab_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_notetab_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
