/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_seadata.h @brief Sea Data asset bundle reader. */

#ifndef XXFCLIB_FORMAT_SEADATA_H
#define XXFCLIB_FORMAT_SEADATA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Sea Data asset bundle: a four-byte magic followed by a chain of stored members, each record announcing the file offset of the record that follows it.
 */
typedef struct xx_seadata {
    Abstractformat format;
    uint64_t number_of_records;
} xx_seadata;

typedef xx_seadata xx_seadata_t;

XXFC_API void xx_seadata_init(xx_seadata *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_seadata *xx_seadata_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_seadata_destroy(xx_seadata *archive);
XXFC_API void xx_seadata_free(xx_seadata *archive);

XXFC_API bool xx_seadata_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_seadata_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_seadata_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_seadata_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_seadata_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_seadata_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_seadata_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_seadata_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_seadata_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SEADATA_H */
