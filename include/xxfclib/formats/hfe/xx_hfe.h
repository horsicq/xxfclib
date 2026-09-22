/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_HFE_H
#define XXFCLIB_FORMAT_HFE_H

#include "xxfclib/formats/xx_format.h"

/* HxC Floppy Emulator image.  Flux cells per cylinder; the member is the MFM-decoded flat image. */
typedef struct xx_hfe {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_hfe;

XXFC_API void xx_hfe_init(xx_hfe *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_hfe *xx_hfe_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_hfe_destroy(xx_hfe *archive);
XXFC_API void xx_hfe_free(xx_hfe *archive);
XXFC_API bool xx_hfe_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_hfe_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_hfe_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_hfe_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_hfe_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hfe_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hfe_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hfe_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hfe_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
