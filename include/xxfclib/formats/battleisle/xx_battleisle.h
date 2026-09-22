/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_battleisle.h @brief Battle Isle .LIB container reader. */

#ifndef XXFCLIB_FORMAT_BATTLEISLE_H
#define XXFCLIB_FORMAT_BATTLEISLE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Battle Isle .LIB container: a 32-bit little-endian pointer to a
 * trailing directory of 12-byte (8-byte name, 32-bit absolute offset) entries,
 * with the stored members laid out contiguously in front of it.
 */
typedef struct xx_battleisle {
    Abstractformat format;
    uint64_t number_of_records;
} xx_battleisle;

typedef xx_battleisle xx_battleisle_t;

XXFC_API void xx_battleisle_init(xx_battleisle *archive, xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_battleisle *xx_battleisle_create(xx_io_device *device,
                                             int64_t base_address);
XXFC_API void xx_battleisle_destroy(xx_battleisle *archive);
XXFC_API void xx_battleisle_free(xx_battleisle *archive);

XXFC_API bool xx_battleisle_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_battleisle_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_battleisle_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_battleisle_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_battleisle_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_battleisle_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_battleisle_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_battleisle_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_battleisle_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BATTLEISLE_H */
