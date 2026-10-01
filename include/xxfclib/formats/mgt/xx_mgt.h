/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_mgt.h @brief MGT disk image (+D / DISCiPLE / SAM Coupe) reader. */

#ifndef XXFCLIB_FORMAT_MGT_H
#define XXFCLIB_FORMAT_MGT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An MGT disk image: the 819200-byte sector dump (80 cylinders x 2
 * sides x 10 sectors x 512 bytes, cylinder interleaved) of a +D, DISCiPLE
 * or SAM Coupe (SAMDOS / MasterDOS) floppy.  Side 0 tracks 0..3 hold 80
 * directory entries of 256 bytes; each file is a chain of 510-byte sector
 * payloads.  Members are published as their file data, without the 9-byte
 * DOS file header when the file type carries one.
 */
typedef struct xx_mgt {
    Abstractformat format;
    uint64_t number_of_records;
    uint8_t extra_directory_tracks;
} xx_mgt;

typedef xx_mgt xx_mgt_t;

XXFC_API void xx_mgt_init(xx_mgt *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_mgt *xx_mgt_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_mgt_destroy(xx_mgt *archive);
XXFC_API void xx_mgt_free(xx_mgt *archive);

XXFC_API bool xx_mgt_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mgt_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mgt_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_mgt_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mgt_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mgt_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mgt_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mgt_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mgt_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MGT_H */
