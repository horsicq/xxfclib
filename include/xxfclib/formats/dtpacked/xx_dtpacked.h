/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_dtpacked.h @brief Delrina DT packed file reader. */

#ifndef XXFCLIB_FORMAT_DTPACKED_H
#define XXFCLIB_FORMAT_DTPACKED_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Delrina DT packed file: a 41-byte header carrying the plaintext length and a DOS timestamp, followed by a single PKWARE DCL imploded payload running to end-of-file.
 */
typedef struct xx_dtpacked {
    Abstractformat format;
    uint64_t number_of_records;
} xx_dtpacked;

typedef xx_dtpacked xx_dtpacked_t;

XXFC_API void xx_dtpacked_init(xx_dtpacked *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_dtpacked *xx_dtpacked_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_dtpacked_destroy(xx_dtpacked *archive);
XXFC_API void xx_dtpacked_free(xx_dtpacked *archive);

XXFC_API bool xx_dtpacked_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_dtpacked_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_dtpacked_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_dtpacked_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dtpacked_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dtpacked_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dtpacked_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dtpacked_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dtpacked_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DTPACKED_H */
