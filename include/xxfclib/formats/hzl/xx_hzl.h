/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_hzl.h @brief HZL compressed file reader. */

#ifndef XXFCLIB_FORMAT_HZL_H
#define XXFCLIB_FORMAT_HZL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An HZL compressed file: a twelve-byte '!HZL' header carrying the plaintext length and the original file's extension, followed by a single LZHUF stream that runs to end-of-file.
 */
typedef struct xx_hzl {
    Abstractformat format;
    uint64_t number_of_records;
} xx_hzl;

typedef xx_hzl xx_hzl_t;

XXFC_API void xx_hzl_init(xx_hzl *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_hzl *xx_hzl_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_hzl_destroy(xx_hzl *archive);
XXFC_API void xx_hzl_free(xx_hzl *archive);

XXFC_API bool xx_hzl_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_hzl_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_hzl_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_hzl_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_hzl_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hzl_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hzl_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hzl_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hzl_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HZL_H */
