/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_CSIDOS_H
#define XXFCLIB_FORMAT_CSIDOS_H

#include "xxfclib/formats/xx_format.h"

/* CSI-DOS floppy images for the Soviet Elektronika BK-0011M (.IMG/.BKD).
 * A flat 512-byte block device whose catalogue lives in blocks 2..9 and
 * carries a one-byte directory identifier per entry. */
typedef struct xx_csidos {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t image_end;
} xx_csidos;

XXFC_API void xx_csidos_init(xx_csidos *image, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_csidos *xx_csidos_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_csidos_destroy(xx_csidos *image);
XXFC_API void xx_csidos_free(xx_csidos *image);
XXFC_API bool xx_csidos_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_csidos_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_csidos_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_csidos_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_csidos_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_csidos_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_csidos_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_csidos_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_csidos_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
