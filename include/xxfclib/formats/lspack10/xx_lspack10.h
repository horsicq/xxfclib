/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lspack10.h @brief An LSPack 1.0 ".LSP" archive: a chain of 40-byte ZIP-like local headers, each followed by its name, its directory path and a raw Deflate stream. */

#ifndef XXFCLIB_FORMAT_LSPACK10_H
#define XXFCLIB_FORMAT_LSPACK10_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An LSPack 1.0 ".LSP" archive: a chain of 40-byte ZIP-like local headers, each followed by its name, its directory path and a raw Deflate stream.
 */
typedef struct xx_lspack10 {
    Abstractformat format;
    uint64_t number_of_records;
} xx_lspack10;

typedef xx_lspack10 xx_lspack10_t;

XXFC_API void xx_lspack10_init(xx_lspack10 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_lspack10 *xx_lspack10_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_lspack10_destroy(xx_lspack10 *archive);
XXFC_API void xx_lspack10_free(xx_lspack10 *archive);

XXFC_API bool xx_lspack10_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lspack10_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_lspack10_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_lspack10_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lspack10_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lspack10_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lspack10_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lspack10_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lspack10_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LSPACK10_H */
