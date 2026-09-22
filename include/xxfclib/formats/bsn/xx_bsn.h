/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_bsn.h @brief A BSA ".BSN" archive: a 6-byte archive header followed by a chain of CRC-protected member headers and their payloads, ending on a two-byte zero trailer. */

#ifndef XXFCLIB_FORMAT_BSN_H
#define XXFCLIB_FORMAT_BSN_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A BSA ".BSN" archive: a 6-byte archive header followed by a chain of CRC-protected member headers and their payloads, ending on a two-byte zero trailer.
 */
typedef struct xx_bsn {
    Abstractformat format;
    uint64_t number_of_records;
} xx_bsn;

typedef xx_bsn xx_bsn_t;

XXFC_API void xx_bsn_init(xx_bsn *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_bsn *xx_bsn_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_bsn_destroy(xx_bsn *archive);
XXFC_API void xx_bsn_free(xx_bsn *archive);

XXFC_API bool xx_bsn_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bsn_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_bsn_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_bsn_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bsn_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bsn_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bsn_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bsn_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bsn_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BSN_H */
