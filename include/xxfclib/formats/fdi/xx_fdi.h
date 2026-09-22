/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_FDI_H
#define XXFCLIB_FORMAT_FDI_H

#include "xxfclib/formats/xx_format.h"

/* FDI floppy image.  The header points at a stored sector area, which is the single member. */
typedef struct xx_fdi {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_fdi;

XXFC_API void xx_fdi_init(xx_fdi *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_fdi *xx_fdi_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_fdi_destroy(xx_fdi *archive);
XXFC_API void xx_fdi_free(xx_fdi *archive);
XXFC_API bool xx_fdi_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_fdi_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_fdi_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_fdi_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_fdi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_fdi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_fdi_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_fdi_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_fdi_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
