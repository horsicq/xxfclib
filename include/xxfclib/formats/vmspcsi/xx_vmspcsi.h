/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_VMSPCSI_H
#define XXFCLIB_FORMAT_VMSPCSI_H

#include "xxfclib/formats/xx_format.h"

/* OpenVMS PCSI$COMPRESSED -- "OpenVMS DCX PCSI Compressed File".  A single
 * member (the PCSI kit, always written back out as FILE.PCSI) coded against
 * DCX context tables that live in a blob of their own near the front. */
typedef struct xx_vmspcsi {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_vmspcsi;

XXFC_API void xx_vmspcsi_init(xx_vmspcsi *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_vmspcsi *xx_vmspcsi_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_vmspcsi_destroy(xx_vmspcsi *archive);
XXFC_API void xx_vmspcsi_free(xx_vmspcsi *archive);
XXFC_API bool xx_vmspcsi_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_vmspcsi_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_vmspcsi_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_vmspcsi_get_number_of_archive_records(Abstractformat *self,
                                                           xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_vmspcsi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vmspcsi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vmspcsi_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_vmspcsi_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vmspcsi_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
