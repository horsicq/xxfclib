/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_fls.h @brief IBM SaveRam FLS archive reader. */

#ifndef XXFCLIB_FORMAT_FLS_H
#define XXFCLIB_FORMAT_FLS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IBM SaveRam/SaveRam2 FLS archive: a 16-bit record count, a table of fixed 44-byte records, a 'SaveRam'-tagged name block of length-prefixed path components, and the member payloads.
 */
typedef struct xx_fls {
    Abstractformat format;
    uint64_t number_of_records;
} xx_fls;

typedef xx_fls xx_fls_t;

XXFC_API void xx_fls_init(xx_fls *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_fls *xx_fls_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_fls_destroy(xx_fls *archive);
XXFC_API void xx_fls_free(xx_fls *archive);

XXFC_API bool xx_fls_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_fls_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_fls_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_fls_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_fls_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_fls_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_fls_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_fls_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_fls_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FLS_H */
