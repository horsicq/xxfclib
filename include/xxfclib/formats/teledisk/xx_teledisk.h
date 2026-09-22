/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_TELEDISK_H
#define XXFCLIB_FORMAT_TELEDISK_H

#include "xxfclib/formats/xx_format.h"

/* Sydex TeleDisk .TD0 image.  "TD" is stored, "td" is compressed (LZW below v20, LZHUF at v20/21). */
typedef struct xx_teledisk {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_teledisk;

XXFC_API void xx_teledisk_init(xx_teledisk *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_teledisk *xx_teledisk_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_teledisk_destroy(xx_teledisk *archive);
XXFC_API void xx_teledisk_free(xx_teledisk *archive);
XXFC_API bool xx_teledisk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_teledisk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_teledisk_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_teledisk_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_teledisk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_teledisk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_teledisk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_teledisk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_teledisk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
