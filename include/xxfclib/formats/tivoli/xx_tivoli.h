/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_tivoli.h @brief Tivoli Filepack Block archive reader. */

#ifndef XXFCLIB_FORMAT_TIVOLI_H
#define XXFCLIB_FORMAT_TIVOLI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Tivoli Filepack Block container: a 79-byte ASCII header line followed by a chain of stored and LZ77-compressed blocks, each ending in eight bytes of a running MD5, which together carry one unwrapped stream that is usually a cpio 'newc' archive.
 */
typedef struct xx_tivoli {
    Abstractformat format;
    uint64_t number_of_records;
} xx_tivoli;

typedef xx_tivoli xx_tivoli_t;

XXFC_API void xx_tivoli_init(xx_tivoli *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_tivoli *xx_tivoli_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_tivoli_destroy(xx_tivoli *archive);
XXFC_API void xx_tivoli_free(xx_tivoli *archive);

XXFC_API bool xx_tivoli_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_tivoli_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_tivoli_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_tivoli_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tivoli_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tivoli_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tivoli_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tivoli_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tivoli_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TIVOLI_H */
