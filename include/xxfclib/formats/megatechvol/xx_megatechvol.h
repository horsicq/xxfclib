/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_megatechvol.h @brief Megatech Software .VOL resource volume reader. */

#ifndef XXFCLIB_FORMAT_MEGATECHVOL_H
#define XXFCLIB_FORMAT_MEGATECHVOL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Megatech Software .VOL volume: a bare table of 32-bit absolute
 * offsets whose first entry doubles as the table length, with stored bodies
 * running between consecutive offsets and no names anywhere.
 */
typedef struct xx_megatechvol {
    Abstractformat format;
    uint64_t number_of_records;
} xx_megatechvol;

typedef xx_megatechvol xx_megatechvol_t;

XXFC_API void xx_megatechvol_init(xx_megatechvol *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_megatechvol *xx_megatechvol_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_megatechvol_destroy(xx_megatechvol *archive);
XXFC_API void xx_megatechvol_free(xx_megatechvol *archive);

XXFC_API bool xx_megatechvol_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_megatechvol_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_megatechvol_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_megatechvol_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_megatechvol_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_megatechvol_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_megatechvol_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_megatechvol_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_megatechvol_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MEGATECHVOL_H */
