/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_saf.h @brief Stac Electronics SAF archive reader. */

#ifndef XXFCLIB_FORMAT_SAF_H
#define XXFCLIB_FORMAT_SAF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Stac SAF archive: a fixed-length "SAF, (c)" banner terminated by
 * 1A 00, then a chain of 0x23-byte member headers each immediately followed
 * by its packed bytes.  There is no central directory and no terminator: the
 * chain ends when fewer than one header's worth of bytes remain.
 */
typedef struct xx_saf {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t first_member_offset; /**< absolute offset of the first header */
} xx_saf;

typedef xx_saf xx_saf_t;

XXFC_API void xx_saf_init(xx_saf *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_saf *xx_saf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_saf_destroy(xx_saf *archive);
XXFC_API void xx_saf_free(xx_saf *archive);

XXFC_API bool xx_saf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_saf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_saf_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_saf_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_saf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_saf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_saf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_saf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_saf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SAF_H */
