/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_mozilla_mar.h @brief Mozilla ARchive (Firefox update MAR) reader. */
#ifndef XXFCLIB_FORMAT_MOZILLA_MAR_H
#define XXFCLIB_FORMAT_MOZILLA_MAR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_mozilla_mar {
    Abstractformat format;
} xx_mozilla_mar;

XXFC_API void xx_mozilla_mar_init(xx_mozilla_mar *archive,
                                   xx_io_device *device,
                                   int64_t base_address);
XXFC_API xx_mozilla_mar *xx_mozilla_mar_create(xx_io_device *device,
                                                int64_t base_address);
XXFC_API void xx_mozilla_mar_destroy(xx_mozilla_mar *archive);
XXFC_API void xx_mozilla_mar_free(xx_mozilla_mar *archive);
XXFC_API bool xx_mozilla_mar_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_mozilla_mar_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_mozilla_mar_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_mozilla_mar_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_mozilla_mar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mozilla_mar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mozilla_mar_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
/** Extract the indexed member bytes (without applying update patches). */
XXFC_API bool xx_mozilla_mar_extract_record_to_device(
    Abstractformat *self, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd);
XXFC_API bool xx_mozilla_mar_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mozilla_mar_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_FORMAT_MOZILLA_MAR_H */
