/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_wintersoft.h @brief Wintersoft archive reader. */

#ifndef XXFCLIB_FORMAT_WINTERSOFT_H
#define XXFCLIB_FORMAT_WINTERSOFT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Wintersoft "**++" archive: an 8-byte header naming one codec for the whole file, followed by a chain of 8-byte length records each immediately followed by its compressed member.
 */
typedef struct xx_wintersoft {
    Abstractformat format;
    uint64_t number_of_records;
} xx_wintersoft;

typedef xx_wintersoft xx_wintersoft_t;

XXFC_API void xx_wintersoft_init(xx_wintersoft *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_wintersoft *xx_wintersoft_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_wintersoft_destroy(xx_wintersoft *archive);
XXFC_API void xx_wintersoft_free(xx_wintersoft *archive);

XXFC_API bool xx_wintersoft_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_wintersoft_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_wintersoft_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_wintersoft_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_wintersoft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wintersoft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wintersoft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wintersoft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wintersoft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WINTERSOFT_H */
