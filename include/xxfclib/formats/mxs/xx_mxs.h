/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_mxs.h @brief MXS packed archive reader. */

#ifndef XXFCLIB_FORMAT_MXS_H
#define XXFCLIB_FORMAT_MXS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An MXS archive: a chain of members, each a seventeen byte header
 * carrying the packed length and a twelve character name, followed by an
 * LZHUF stream that opens with its own plaintext length.
 */
typedef struct xx_mxs {
    Abstractformat format;
    uint64_t number_of_records;
} xx_mxs;

typedef xx_mxs xx_mxs_t;

XXFC_API void xx_mxs_init(xx_mxs *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_mxs *xx_mxs_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_mxs_destroy(xx_mxs *archive);
XXFC_API void xx_mxs_free(xx_mxs *archive);

XXFC_API bool xx_mxs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mxs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mxs_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_mxs_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mxs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mxs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mxs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mxs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mxs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MXS_H */
