/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sqx.h @brief SQX archive reader. */

#ifndef XXFCLIB_FORMAT_SQX_H
#define XXFCLIB_FORMAT_SQX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An SQX archive: a 25-byte main header followed by a chain of self-sized records, each file record carrying its payload immediately behind its own header and any extension headers.
 */
typedef struct xx_sqx {
    Abstractformat format;
    uint64_t number_of_records;
} xx_sqx;

typedef xx_sqx xx_sqx_t;

XXFC_API void xx_sqx_init(xx_sqx *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_sqx *xx_sqx_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_sqx_destroy(xx_sqx *archive);
XXFC_API void xx_sqx_free(xx_sqx *archive);

XXFC_API bool xx_sqx_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_sqx_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_sqx_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_sqx_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sqx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sqx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sqx_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sqx_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sqx_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SQX_H */
