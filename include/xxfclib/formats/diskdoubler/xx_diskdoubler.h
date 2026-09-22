/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_diskdoubler.h @brief DiskDoubler compressed file reader. */

#ifndef XXFCLIB_FORMAT_DISKDOUBLER_H
#define XXFCLIB_FORMAT_DISKDOUBLER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A DiskDoubler compressed Macintosh file: one 84-byte big-endian header carrying an independent packed length, plaintext length and codec number for each of the file's two Macintosh forks, followed by the data fork's packed bytes and then the resource fork's.
 */
typedef struct xx_diskdoubler {
    Abstractformat format;
    uint64_t number_of_records;
} xx_diskdoubler;

typedef xx_diskdoubler xx_diskdoubler_t;

XXFC_API void xx_diskdoubler_init(xx_diskdoubler *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_diskdoubler *xx_diskdoubler_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_diskdoubler_destroy(xx_diskdoubler *archive);
XXFC_API void xx_diskdoubler_free(xx_diskdoubler *archive);

XXFC_API bool xx_diskdoubler_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_diskdoubler_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_diskdoubler_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_diskdoubler_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_diskdoubler_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_diskdoubler_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_diskdoubler_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_diskdoubler_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_diskdoubler_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DISKDOUBLER_H */
