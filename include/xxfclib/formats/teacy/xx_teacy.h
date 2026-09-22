/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_teacy.h @brief A Teacy ".DAT" archive: a 16-bit member count followed by a directory of 24-byte records and stored payloads. */

#ifndef XXFCLIB_FORMAT_TEACY_H
#define XXFCLIB_FORMAT_TEACY_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Teacy ".DAT" archive: a 16-bit member count followed by a directory of 24-byte records and stored payloads.
 */
typedef struct xx_teacy {
    Abstractformat format;
    uint64_t number_of_records;
} xx_teacy;

typedef xx_teacy xx_teacy_t;

XXFC_API void xx_teacy_init(xx_teacy *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_teacy *xx_teacy_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_teacy_destroy(xx_teacy *archive);
XXFC_API void xx_teacy_free(xx_teacy *archive);

XXFC_API bool xx_teacy_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_teacy_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_teacy_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_teacy_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_teacy_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_teacy_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_teacy_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_teacy_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_teacy_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TEACY_H */
