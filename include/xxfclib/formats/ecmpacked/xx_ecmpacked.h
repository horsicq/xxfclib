/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ecmpacked.h @brief EmmaSetup packed member reader. */

#ifndef XXFCLIB_FORMAT_ECMPACKED_H
#define XXFCLIB_FORMAT_ECMPACKED_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An EmmaSetup packed member: a 38-byte 'ECM\0' header followed by a single Okumura LZSS stream running to end-of-file.
 */
typedef struct xx_ecmpacked {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ecmpacked;

typedef xx_ecmpacked xx_ecmpacked_t;

XXFC_API void xx_ecmpacked_init(xx_ecmpacked *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ecmpacked *xx_ecmpacked_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ecmpacked_destroy(xx_ecmpacked *archive);
XXFC_API void xx_ecmpacked_free(xx_ecmpacked *archive);

XXFC_API bool xx_ecmpacked_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ecmpacked_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ecmpacked_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ecmpacked_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ecmpacked_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ecmpacked_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ecmpacked_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ecmpacked_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ecmpacked_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ECMPACKED_H */
