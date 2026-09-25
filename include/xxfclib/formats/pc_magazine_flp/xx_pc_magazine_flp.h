/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_PC_MAGAZINE_FLP_H
#define XXFCLIB_FORMAT_PC_MAGAZINE_FLP_H

#include "xxfclib/formats/xx_format.h"

/* PC Magazine FLP floppy image: a 13-byte "PCM" header carrying the disk
 * geometry, then the stored sectors in plain cylinder/head/sector order. */
typedef struct xx_pc_magazine_flp {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t version;
    uint32_t sides;
    uint32_t tracks;
    uint32_t sectors_per_track;
    uint32_t sector_size;
    uint64_t image_size;
} xx_pc_magazine_flp;

XXFC_API void xx_pc_magazine_flp_init(xx_pc_magazine_flp *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_pc_magazine_flp *xx_pc_magazine_flp_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_pc_magazine_flp_destroy(xx_pc_magazine_flp *archive);
XXFC_API void xx_pc_magazine_flp_free(xx_pc_magazine_flp *archive);
XXFC_API bool xx_pc_magazine_flp_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_pc_magazine_flp_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_pc_magazine_flp_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_pc_magazine_flp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *
xx_pc_magazine_flp_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pc_magazine_flp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pc_magazine_flp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pc_magazine_flp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pc_magazine_flp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
