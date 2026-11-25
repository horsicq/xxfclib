/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_X68000_DIM_H
#define XXFCLIB_FORMAT_X68000_DIM_H

#include "xxfclib/formats/xx_format.h"

/* Sharp X68000 DIM floppy image (DIFC.X): a 256-byte header holding the
 * media type, a 170-entry track-present map and "DIFC HEADER" at 0xAB,
 * followed by every track at a fixed position.  The single member is the
 * raw sector dump of the disk, with tracks the map marks absent filled
 * with 0xE5. */
typedef struct xx_x68000_dim {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t media_type;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t sectors_per_track;
    uint32_t sector_size;
    uint32_t track_size;
    uint32_t present_tracks;
    uint32_t stored_tracks;
    uint64_t image_size;
} xx_x68000_dim;

XXFC_API void xx_x68000_dim_init(xx_x68000_dim *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_x68000_dim *xx_x68000_dim_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_x68000_dim_destroy(xx_x68000_dim *archive);
XXFC_API void xx_x68000_dim_free(xx_x68000_dim *archive);
XXFC_API bool xx_x68000_dim_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_x68000_dim_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_x68000_dim_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_x68000_dim_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_x68000_dim_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_x68000_dim_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_x68000_dim_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_x68000_dim_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_x68000_dim_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_x68000_dim_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_x68000_dim_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_x68000_dim_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_x68000_dim_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_x68000_dim_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
