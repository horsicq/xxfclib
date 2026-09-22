/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_ARCFS_H
#define XXFCLIB_FORMAT_ARCFS_H

#include "xxfclib/formats/xx_format.h"

typedef struct xx_arcfs {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_arcfs;

XXFC_API void xx_arcfs_init(xx_arcfs *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_arcfs *xx_arcfs_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_arcfs_destroy(xx_arcfs *archive);
XXFC_API void xx_arcfs_free(xx_arcfs *archive);
XXFC_API bool xx_arcfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_arcfs_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_arcfs_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_arcfs_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_arcfs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_arcfs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_arcfs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_arcfs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_arcfs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
