/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_marc.h @brief MARC archive reader. */

#ifndef XXFCLIB_FORMAT_MARC_H
#define XXFCLIB_FORMAT_MARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A MARC archive: a fixed-size directory of stored members, each named in a 56-byte field.
 */
typedef struct xx_marc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_marc;

typedef xx_marc xx_marc_t;

XXFC_API void xx_marc_init(xx_marc *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_marc *xx_marc_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_marc_destroy(xx_marc *archive);
XXFC_API void xx_marc_free(xx_marc *archive);

XXFC_API bool xx_marc_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_marc_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_marc_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_marc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_marc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_marc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_marc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_marc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_marc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MARC_H */
