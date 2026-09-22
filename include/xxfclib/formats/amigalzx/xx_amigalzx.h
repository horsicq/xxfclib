/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_amigalzx.h @brief Amiga LZX archive reader. */

#ifndef XXFCLIB_FORMAT_AMIGALZX_H
#define XXFCLIB_FORMAT_AMIGALZX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Amiga LZX archive: a "LZX" signature followed by a chain of 31-byte member headers, each carrying its name and comment inline, where consecutive members share one compressed stream and only the last member of such a group states the stream's packed length.
 */
typedef struct xx_amigalzx {
    Abstractformat format;
    uint64_t number_of_records;
} xx_amigalzx;

typedef xx_amigalzx xx_amigalzx_t;

XXFC_API void xx_amigalzx_init(xx_amigalzx *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_amigalzx *xx_amigalzx_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_amigalzx_destroy(xx_amigalzx *archive);
XXFC_API void xx_amigalzx_free(xx_amigalzx *archive);

XXFC_API bool xx_amigalzx_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_amigalzx_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_amigalzx_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_amigalzx_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_amigalzx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_amigalzx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_amigalzx_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_amigalzx_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_amigalzx_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_AMIGALZX_H */
