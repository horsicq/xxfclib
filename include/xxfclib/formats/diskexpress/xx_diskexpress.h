/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_DISKEXPRESS_H
#define XXFCLIB_FORMAT_DISKEXPRESS_H

#include "xxfclib/formats/xx_format.h"

/* Disk eXPress ".DXP": a floppy image behind a 512-byte "AS" header, either
 * stored whole or as independently compressed tracks.  One record. */
typedef struct xx_diskexpress {
    Abstractformat format;
    int64_t archive_end;
    int64_t image_size;
    uint32_t data_crc;
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t disk_type;
    uint8_t compression_method;
} xx_diskexpress;

XXFC_API void xx_diskexpress_init(xx_diskexpress *archive,
                                  xx_io_device *device, int64_t base_address);
XXFC_API xx_diskexpress *xx_diskexpress_create(xx_io_device *device,
                                               int64_t base_address);
XXFC_API void xx_diskexpress_destroy(xx_diskexpress *archive);
XXFC_API void xx_diskexpress_free(xx_diskexpress *archive);
XXFC_API bool xx_diskexpress_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_diskexpress_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_diskexpress_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_diskexpress_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *
xx_diskexpress_create_archive_records_reading(Abstractformat *self,
                                              const xx_list_s *options,
                                              xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_diskexpress_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_diskexpress_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_diskexpress_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_diskexpress_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
