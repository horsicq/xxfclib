/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_SHRINKWRAP_H
#define XXFCLIB_FORMAT_SHRINKWRAP_H

#include "xxfclib/formats/xx_format.h"

/* Shrink-Wrap / Apple "Disk Copy 4.2" floppy image: an 84-byte header, the
 * stored sector data, and optionally the GCR tag bytes. */
typedef struct xx_shrinkwrap {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    int64_t data_size;
    int64_t tag_size;
    uint8_t disk_format;
    uint8_t format_byte;
} xx_shrinkwrap;

XXFC_API void xx_shrinkwrap_init(xx_shrinkwrap *archive, xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_shrinkwrap *xx_shrinkwrap_create(xx_io_device *device,
                                             int64_t base_address);
XXFC_API void xx_shrinkwrap_destroy(xx_shrinkwrap *archive);
XXFC_API void xx_shrinkwrap_free(xx_shrinkwrap *archive);
XXFC_API bool xx_shrinkwrap_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_shrinkwrap_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_shrinkwrap_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_shrinkwrap_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *
xx_shrinkwrap_create_archive_records_reading(Abstractformat *self,
                                             const xx_list_s *options,
                                             xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_shrinkwrap_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_shrinkwrap_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_shrinkwrap_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_shrinkwrap_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
