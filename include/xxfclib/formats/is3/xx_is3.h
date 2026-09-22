/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_IS3_H
#define XXFCLIB_FORMAT_IS3_H

#include "xxfclib/formats/xx_format.h"

/* InstallShield 3 installer data files.  Two related containers share this
 * reader:
 *   - the ".z" / "DATA.n" cabinet, magic 0x8C655D13, with a directory table
 *     and a file table at the end of the volume;
 *   - the "_INST32I.EX_" / "_INST16.EX_" payload archive, magic 0xD879AB2A,
 *     whose table sits directly behind the copyright banner.
 * Members of both are either stored or PKWARE DCL imploded. */
typedef struct xx_is3 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_is3;

XXFC_API void xx_is3_init(xx_is3 *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_is3 *xx_is3_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_is3_destroy(xx_is3 *archive);
XXFC_API void xx_is3_free(xx_is3 *archive);
XXFC_API bool xx_is3_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_is3_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_is3_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_is3_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_is3_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_is3_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_is3_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_is3_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_is3_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
