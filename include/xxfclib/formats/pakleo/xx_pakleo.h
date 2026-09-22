/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_pakleo.h @brief PAKLEO archive reader. */

#ifndef XXFCLIB_FORMAT_PAKLEO_H
#define XXFCLIB_FORMAT_PAKLEO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A PAKLEO (.PLL) archive: a 37-byte ASCII banner followed by a chain of 26-byte records, each carrying its name inline and its LEOLZW or stored payload immediately after it.
 */
typedef struct xx_pakleo {
    Abstractformat format;
    uint64_t number_of_records;
} xx_pakleo;

typedef xx_pakleo xx_pakleo_t;

XXFC_API void xx_pakleo_init(xx_pakleo *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_pakleo *xx_pakleo_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_pakleo_destroy(xx_pakleo *archive);
XXFC_API void xx_pakleo_free(xx_pakleo *archive);

XXFC_API bool xx_pakleo_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_pakleo_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_pakleo_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_pakleo_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pakleo_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pakleo_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pakleo_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pakleo_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pakleo_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PAKLEO_H */
