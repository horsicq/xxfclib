/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lzdiet.h @brief lZdIeT single-file reader. */

#ifndef XXFCLIB_FORMAT_LZDIET_H
#define XXFCLIB_FORMAT_LZDIET_H

#include "xxfclib/algo/lzdiet/xx_lzdiet.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lzdiet {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint16_t chunk_count;
} xx_lzdiet;

typedef xx_lzdiet xx_lzdiet_t;
typedef xx_lzdiet XLzDiet;

XXFC_API void xx_lzdiet_init(xx_lzdiet *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_lzdiet *xx_lzdiet_create(xx_io_device *device,
                                      int64_t base_address);
XXFC_API void xx_lzdiet_destroy(xx_lzdiet *archive);
XXFC_API void xx_lzdiet_free(xx_lzdiet *archive);

XXFC_API bool xx_lzdiet_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_lzdiet_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_lzdiet_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_lzdiet_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lzdiet_unpack_to_device(xx_lzdiet *archive,
                                          xx_io_device *destination,
                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzdiet_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzdiet_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzdiet_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzdiet_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzdiet_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_lzdiet_get_uncompressed_size(
    const xx_lzdiet *archive);
XXFC_API int64_t xx_lzdiet_get_stream_end(const xx_lzdiet *archive);
XXFC_API uint16_t xx_lzdiet_get_chunk_count(const xx_lzdiet *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZDIET_H */
