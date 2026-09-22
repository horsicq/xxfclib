/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_asymetrix.h @brief Asymetrix Setup archive reader. */

#ifndef XXFCLIB_FORMAT_ASYMETRIX_H
#define XXFCLIB_FORMAT_ASYMETRIX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Asymetrix ToolBook Setup disk-set volume: a 0x2C-byte header naming the set and the volume number, optionally followed by the set's fixed-stride member directory, and then the members themselves stored as chains of independently compressed 4096-byte blocks.
 */
typedef struct xx_asymetrix {
    Abstractformat format;
    uint64_t number_of_records;
} xx_asymetrix;

typedef xx_asymetrix xx_asymetrix_t;

XXFC_API void xx_asymetrix_init(xx_asymetrix *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_asymetrix *xx_asymetrix_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_asymetrix_destroy(xx_asymetrix *archive);
XXFC_API void xx_asymetrix_free(xx_asymetrix *archive);

XXFC_API bool xx_asymetrix_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_asymetrix_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_asymetrix_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_asymetrix_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_asymetrix_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_asymetrix_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_asymetrix_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_asymetrix_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_asymetrix_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ASYMETRIX_H */
