/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_jbf.h @brief JBF archive reader. */

#ifndef XXFCLIB_FORMAT_JBF_H
#define XXFCLIB_FORMAT_JBF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A JBF archive: a headerless run of LZHUF member streams followed by a trailing directory of 31-byte entries and a one-byte member count as the very last byte of the file.
 */
typedef struct xx_jbf {
    Abstractformat format;
    uint64_t number_of_records;
} xx_jbf;

typedef xx_jbf xx_jbf_t;

XXFC_API void xx_jbf_init(xx_jbf *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_jbf *xx_jbf_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_jbf_destroy(xx_jbf *archive);
XXFC_API void xx_jbf_free(xx_jbf *archive);

XXFC_API bool xx_jbf_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_jbf_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_jbf_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_jbf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_jbf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_jbf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_jbf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_jbf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_jbf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_JBF_H */
