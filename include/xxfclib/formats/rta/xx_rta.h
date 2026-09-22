/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_rta.h @brief Pocket Soft RTPatch archive reader. */

#ifndef XXFCLIB_FORMAT_RTA_H
#define XXFCLIB_FORMAT_RTA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Pocket Soft RTA archive: the four-byte signature "KJd\0" followed by a chain of variable-length records, each a counted name, a counted second string, a 13-byte fixed part and then the member's RTPatch-compressed bytes, ended by a zero length byte.
 */
typedef struct xx_rta {
    Abstractformat format;
    uint64_t number_of_records;
} xx_rta;

typedef xx_rta xx_rta_t;

XXFC_API void xx_rta_init(xx_rta *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_rta *xx_rta_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_rta_destroy(xx_rta *archive);
XXFC_API void xx_rta_free(xx_rta *archive);

XXFC_API bool xx_rta_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_rta_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_rta_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_rta_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rta_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rta_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rta_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rta_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rta_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RTA_H */
