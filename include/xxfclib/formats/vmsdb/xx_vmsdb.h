/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_VMSDB_H
#define XXFCLIB_FORMAT_VMSDB_H

#include "xxfclib/formats/xx_format.h"

/* VMS DataBase -- the OpenVMS PCSI$ product kit an "OpenVMS DCX PCSI"
 * container expands to.  A BER-like TLV tree, not a record chain. */
typedef struct xx_vmsdb {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_vmsdb;

XXFC_API void xx_vmsdb_init(xx_vmsdb *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_vmsdb *xx_vmsdb_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_vmsdb_destroy(xx_vmsdb *archive);
XXFC_API void xx_vmsdb_free(xx_vmsdb *archive);
XXFC_API bool xx_vmsdb_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_vmsdb_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_vmsdb_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_vmsdb_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_vmsdb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vmsdb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vmsdb_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_vmsdb_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vmsdb_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
