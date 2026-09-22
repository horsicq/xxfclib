/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lzpis2.h @brief LZPIS2 single-member archive reader. */

#ifndef XXFCLIB_FORMAT_LZPIS2_H
#define XXFCLIB_FORMAT_LZPIS2_H

#include "xxfclib/algo/lzpis2/xx_lzpis2.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_lzpis2 {
    Abstractformat format;
    uint64_t uncompressed_size;
    uint32_t chunk_count;
} xx_lzpis2;

typedef xx_lzpis2 xx_lzpis2_t;
typedef xx_lzpis2 XLzpis2;

XXFC_API void xx_lzpis2_init(xx_lzpis2 *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_lzpis2 *xx_lzpis2_create(xx_io_device *device,
                                      int64_t base_address);
XXFC_API void xx_lzpis2_destroy(xx_lzpis2 *archive);
XXFC_API void xx_lzpis2_free(xx_lzpis2 *archive);

XXFC_API bool xx_lzpis2_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_lzpis2_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_lzpis2_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_lzpis2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lzpis2_unpack_to_device(xx_lzpis2 *archive,
                                          xx_io_device *destination,
                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzpis2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzpis2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzpis2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzpis2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzpis2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_lzpis2_get_uncompressed_size(
    const xx_lzpis2 *archive);
XXFC_API uint32_t xx_lzpis2_get_chunk_count(const xx_lzpis2 *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZPIS2_H */
