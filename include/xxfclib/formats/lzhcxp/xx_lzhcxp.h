/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lzhcxp.h @brief LZ compressed file reader. */

#ifndef XXFCLIB_FORMAT_LZHCXP_H
#define XXFCLIB_FORMAT_LZHCXP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An "LZ" single-file container: two magic bytes and nothing else, followed by a chain of length-prefixed blocks carrying a block-framed LZW stream whose decompressed size is stated nowhere and must be measured by decoding.
 */
typedef struct xx_lzhcxp {
    Abstractformat format;
    uint64_t number_of_records;
} xx_lzhcxp;

typedef xx_lzhcxp xx_lzhcxp_t;

XXFC_API void xx_lzhcxp_init(xx_lzhcxp *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_lzhcxp *xx_lzhcxp_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_lzhcxp_destroy(xx_lzhcxp *archive);
XXFC_API void xx_lzhcxp_free(xx_lzhcxp *archive);

XXFC_API bool xx_lzhcxp_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_lzhcxp_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_lzhcxp_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_lzhcxp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzhcxp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzhcxp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzhcxp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzhcxp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzhcxp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZHCXP_H */
